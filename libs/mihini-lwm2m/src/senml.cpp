#include "mihini/lwm2m/senml.hpp"

#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace mihini::lwm2m::senml {

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xf];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string number_string(double v, bool is_float) {
    if (is_float) return std::to_string(v);
    return std::to_string(static_cast<long long>(v));
}

const char* b64url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size()) {
        const std::uint32_t n = (static_cast<std::uint8_t>(in[i]) << 16) |
                                (static_cast<std::uint8_t>(in[i + 1]) << 8) |
                                static_cast<std::uint8_t>(in[i + 2]);
        out += b64url[(n >> 18) & 63];
        out += b64url[(n >> 12) & 63];
        out += b64url[(n >> 6) & 63];
        out += b64url[n & 63];
        i += 3;
    }
    const std::size_t rem = in.size() - i;
    if (rem == 1) {
        const std::uint32_t n = static_cast<std::uint8_t>(in[i]) << 16;
        out += b64url[(n >> 18) & 63];
        out += b64url[(n >> 12) & 63];
    } else if (rem == 2) {
        const std::uint32_t n = (static_cast<std::uint8_t>(in[i]) << 16) |
                                (static_cast<std::uint8_t>(in[i + 1]) << 8);
        out += b64url[(n >> 18) & 63];
        out += b64url[(n >> 12) & 63];
        out += b64url[(n >> 6) & 63];
    }
    return out;
}

int b64url_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

bool base64url_decode(const std::string& in, std::string& out) {
    out.clear();
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') break;
        const int v = b64url_value(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xff));
        }
    }
    return true;
}

// Minimal recursive-descent JSON value model (subset sufficient for SenML).
struct JsonValue {
    enum class T { Null, Bool, Number, String, Object, Array } type{T::Null};
    bool boolean{false};
    double number{0.0};
    bool number_is_float{false};
    std::string string;
    std::vector<std::pair<std::string, JsonValue>> object;
    std::vector<JsonValue> array;
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    bool parse(JsonValue& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return i_ == s_.size();
    }

private:
    const std::string& s_;
    std::size_t i_{0};

    void skip_ws() {
        while (i_ < s_.size() &&
               (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
            ++i_;
        }
    }

    bool parse_value(JsonValue& out) {
        skip_ws();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') return parse_string(out.string) && (out.type = JsonValue::T::String, true);
        if (c == 't') {
            if (s_.compare(i_, 4, "true") != 0) return false;
            out.type = JsonValue::T::Bool;
            out.boolean = true;
            i_ += 4;
            return true;
        }
        if (c == 'f') {
            if (s_.compare(i_, 5, "false") != 0) return false;
            out.type = JsonValue::T::Bool;
            out.boolean = false;
            i_ += 5;
            return true;
        }
        if (c == 'n') {
            if (s_.compare(i_, 4, "null") != 0) return false;
            out.type = JsonValue::T::Null;
            i_ += 4;
            return true;
        }
        return parse_number(out);
    }

    bool parse_object(JsonValue& out) {
        out.type = JsonValue::T::Object;
        out.object.clear();
        ++i_; // '{'
        skip_ws();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            out.object.emplace_back();
            out.object.back().first = std::move(key);
            skip_ws();
            if (i_ >= s_.size() || s_[i_] != ':') return false;
            ++i_;
            if (!parse_value(out.object.back().second)) return false;
            skip_ws();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == '}') { ++i_; return true; }
            return false;
        }
    }

    bool parse_array(JsonValue& out) {
        out.type = JsonValue::T::Array;
        out.array.clear();
        ++i_; // '['
        skip_ws();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return true;
        }
        while (true) {
            out.array.emplace_back();
            if (!parse_value(out.array.back())) return false;
            skip_ws();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == ']') { ++i_; return true; }
            return false;
        }
    }

    bool parse_string(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i_ >= s_.size()) return false;
                const char e = s_[i_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (i_ + 4 > s_.size()) return false;
                        std::uint32_t cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            const char h = s_[i_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= h - '0';
                            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                            else return false;
                        }
                        // UTF-8 encode (BMP only, adequate for SenML field names).
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xc0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3f));
                        } else {
                            out += static_cast<char>(0xe0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                            out += static_cast<char>(0x80 | (cp & 0x3f));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out += c;
            }
        }
        return false; // unterminated
    }

    bool parse_number(JsonValue& out) {
        const std::size_t start = i_;
        bool is_float = false;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        if (i_ < s_.size() && s_[i_] == '.') {
            is_float = true;
            ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            is_float = true;
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        if (i_ == start) return false;
        out.type = JsonValue::T::Number;
        out.number_is_float = is_float;
        out.number = std::strtod(s_.c_str() + start, nullptr);
        return true;
    }
};

} // namespace

std::string encode_json(const std::vector<Record>& records) {
    if (records.empty()) return "[]";

    std::string bn;
    bool has_bn = false;
    for (const auto& r : records) {
        if (r.baseName.empty()) continue;
        if (has_bn && bn != r.baseName) return {}; // bn must match across records
        bn = r.baseName;
        has_bn = true;
    }

    std::string out = "[";
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Record& r = records[i];
        out += "{";
        bool first = true;

        if (i == 0 && !r.baseName.empty()) {
            out += "\"bn\":\"" + json_escape(r.baseName) + "\"";
            first = false;
        }
        if (!r.name.empty()) {
            if (!first) out += ",";
            out += "\"n\":\"" + json_escape(r.name) + "\"";
            first = false;
        }
        switch (r.kind) {
            case Kind::Numeric:
                if (!first) out += ",";
                out += "\"v\":" + number_string(r.numericValue, r.isFloat);
                first = false;
                break;
            case Kind::String:
                if (!first) out += ",";
                out += "\"vs\":\"" + json_escape(r.stringValue) + "\"";
                first = false;
                break;
            case Kind::Boolean:
                if (!first) out += ",";
                out += r.booleanValue ? "\"vb\":true" : "\"vb\":false";
                first = false;
                break;
            case Kind::Data:
                if (!first) out += ",";
                out += "\"vd\":\"" + base64url_encode(r.dataValue) + "\"";
                first = false;
                break;
            case Kind::None:
                break;
        }
        out += "}";
        if (i + 1 < records.size()) out += ",";
    }
    out += "]";
    return out;
}

bool decode_json(const std::string& bytes, std::vector<Record>& out) {
    JsonValue root;
    Parser p(bytes);
    if (!p.parse(root) || root.type != JsonValue::T::Array) return false;

    std::string bn;
    for (const auto& item : root.array) {
        if (item.type != JsonValue::T::Object) return false;
        Record r;
        for (const auto& kv : item.object) {
            const std::string& k = kv.first;
            const JsonValue& v = kv.second;
            if (k == "bn" && v.type == JsonValue::T::String) {
                r.baseName = v.string;
            } else if (k == "n" && v.type == JsonValue::T::String) {
                r.name = v.string;
            } else if (k == "v" && v.type == JsonValue::T::Number) {
                r.kind = Kind::Numeric;
                r.numericValue = v.number;
                r.isFloat = v.number_is_float;
            } else if (k == "vs" && v.type == JsonValue::T::String) {
                r.kind = Kind::String;
                r.stringValue = v.string;
            } else if (k == "vb" && v.type == JsonValue::T::Bool) {
                r.kind = Kind::Boolean;
                r.booleanValue = v.boolean;
            } else if (k == "vd" && v.type == JsonValue::T::String) {
                r.kind = Kind::Data;
                if (!base64url_decode(v.string, r.dataValue)) return false;
            }
        }
        if (r.baseName.empty()) r.baseName = bn;
        else bn = r.baseName;
        out.push_back(std::move(r));
    }
    return true;
}

} // namespace mihini::lwm2m::senml
