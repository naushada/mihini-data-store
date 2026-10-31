#include "mihini/lwm2m/coap.hpp"

#include <cstdint>

namespace mihini::lwm2m::coap {

namespace {

// RFC 7252 §3.1 option delta/length nibble scheme.
bool read_ext(const std::string& b, std::size_t& pos, int nibble, std::uint32_t& out) {
    if (nibble < 13) {
        out = static_cast<std::uint32_t>(nibble);
        return true;
    }
    if (nibble == 13) {
        if (pos + 1 > b.size()) return false;
        out = static_cast<std::uint8_t>(b[pos]) + 13;
        pos += 1;
        return true;
    }
    if (nibble == 14) {
        if (pos + 2 > b.size()) return false;
        out = (static_cast<std::uint8_t>(b[pos]) << 8) |
              static_cast<std::uint8_t>(b[pos + 1]);
        out += 269;
        pos += 2;
        return true;
    }
    return false; // 15 is reserved
}

void write_option(std::string& out, std::uint16_t delta, const std::string& value) {
    int dn;
    if (delta < 13) dn = delta;
    else if (delta < 269) dn = 13;
    else dn = 14;

    const std::size_t len = value.size();
    int ln;
    if (len < 13) ln = static_cast<int>(len);
    else if (len < 269) ln = 13;
    else ln = 14;

    out.push_back(static_cast<char>((dn << 4) | ln));

    if (dn == 13) out.push_back(static_cast<char>(delta - 13));
    else if (dn == 14) {
        out.push_back(static_cast<char>(((delta - 269) >> 8) & 0xff));
        out.push_back(static_cast<char>((delta - 269) & 0xff));
    }
    if (ln == 13) out.push_back(static_cast<char>(len - 13));
    else if (ln == 14) {
        out.push_back(static_cast<char>(((len - 269) >> 8) & 0xff));
        out.push_back(static_cast<char>((len - 269) & 0xff));
    }
    out += value;
}

} // namespace

const std::string* Message::find_option(std::uint16_t number) const {
    for (const auto& opt : options) {
        if (opt.number == number) return &opt.value;
    }
    return nullptr;
}

std::string Message::uri_path() const {
    std::string out;
    for (const auto& opt : options) {
        if (opt.number != OPT_URI_PATH) continue;
        out += "/";
        out += opt.value;
    }
    return out;
}

std::string Message::uri_query() const {
    std::string out;
    for (const auto& opt : options) {
        if (opt.number != OPT_URI_QUERY) continue;
        if (!out.empty()) out += "&";
        out += opt.value;
    }
    return out;
}

std::string encode(const Message& m) {
    std::string out;

    const std::size_t tkl = m.token.size() > 8 ? 8 : m.token.size();
    out.push_back(static_cast<char>(0x40 |
                                    (static_cast<std::uint8_t>(m.type) << 4) |
                                    static_cast<std::uint8_t>(tkl)));
    out.push_back(static_cast<char>(static_cast<std::uint8_t>(m.code)));
    out.push_back(static_cast<char>((m.id >> 8) & 0xff));
    out.push_back(static_cast<char>(m.id & 0xff));
    out.append(m.token, 0, tkl);

    std::uint16_t prev = 0;
    for (const auto& opt : m.options) {
        if (opt.number < prev) return {}; // options must be ascending
        write_option(out, static_cast<std::uint16_t>(opt.number - prev), opt.value);
        prev = opt.number;
    }

    if (!m.payload.empty()) {
        out.push_back(static_cast<char>(0xff));
        out += m.payload;
    }
    return out;
}

bool decode(const std::string& bytes, Message& m) {
    if (bytes.size() < 4) return false;

    const std::uint8_t b0 = static_cast<std::uint8_t>(bytes[0]);
    if ((b0 >> 6) != 1) return false; // version must be 1

    m.type = static_cast<Type>((b0 >> 4) & 0x3);
    const std::uint8_t tkl = b0 & 0x0f;
    m.code = static_cast<Code>(static_cast<std::uint8_t>(bytes[1]));
    m.id = static_cast<std::uint16_t>((static_cast<std::uint8_t>(bytes[2]) << 8) |
                                      static_cast<std::uint8_t>(bytes[3]));

    std::size_t pos = 4;
    if (pos + tkl > bytes.size()) return false;
    m.token.assign(bytes, pos, tkl);
    pos += tkl;

    m.options.clear();
    m.payload.clear();

    std::uint16_t prev = 0;
    while (pos < bytes.size()) {
        const std::uint8_t b = static_cast<std::uint8_t>(bytes[pos]);
        if (b == 0xff) {
            m.payload.assign(bytes, pos + 1, std::string::npos);
            return true;
        }
        pos += 1;

        std::uint32_t delta = 0, len = 0;
        if (!read_ext(bytes, pos, b >> 4, delta)) return false;
        if (!read_ext(bytes, pos, b & 0x0f, len)) return false;
        if (pos + len > bytes.size()) return false;

        const std::uint32_t number = prev + delta;
        prev = static_cast<std::uint16_t>(number);

        Option opt;
        opt.number = static_cast<std::uint16_t>(number);
        opt.value.assign(bytes, pos, len);
        pos += len;
        m.options.push_back(std::move(opt));
    }
    return true;
}

} // namespace mihini::lwm2m::coap
