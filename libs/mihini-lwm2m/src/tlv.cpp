#include "mihini/lwm2m/tlv.hpp"

#include <cstdint>

namespace mihini::lwm2m::tlv {

std::string encode(std::uint8_t type, std::uint16_t id, const std::string& value) {
    std::string out;

    const bool id16 = id > 0xff;
    const std::size_t len = value.size();

    int lenbytes;
    if (len == 0) lenbytes = 0;
    else if (len <= 0xff) lenbytes = 1;
    else if (len <= 0xffff) lenbytes = 2;
    else lenbytes = 3;

    std::uint8_t b = static_cast<std::uint8_t>((type & 0x3) << 6);
    if (id16) b |= 0x20;
    b |= static_cast<std::uint8_t>(lenbytes << 3);
    out.push_back(static_cast<char>(b));

    if (id16) {
        out.push_back(static_cast<char>((id >> 8) & 0xff));
        out.push_back(static_cast<char>(id & 0xff));
    } else {
        out.push_back(static_cast<char>(id & 0xff));
    }

    if (lenbytes == 1) {
        out.push_back(static_cast<char>(len & 0xff));
    } else if (lenbytes == 2) {
        out.push_back(static_cast<char>((len >> 8) & 0xff));
        out.push_back(static_cast<char>(len & 0xff));
    } else if (lenbytes == 3) {
        out.push_back(static_cast<char>((len >> 16) & 0xff));
        out.push_back(static_cast<char>((len >> 8) & 0xff));
        out.push_back(static_cast<char>(len & 0xff));
    }

    out += value;
    return out;
}

std::string encode_instance(std::uint16_t iid, const std::vector<Record>& resources) {
    std::string inner;
    for (const auto& r : resources) {
        inner += encode(r.type, r.id, r.value);
    }
    return encode(TYPE_OBJECT_INSTANCE, iid, inner);
}

bool decode(const std::string& bytes, std::vector<Record>& out) {
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const std::uint8_t b = static_cast<std::uint8_t>(bytes[pos++]);

        const std::uint8_t type = (b >> 6) & 0x3;
        const bool id16 = (b & 0x20) != 0;
        const int lenbytes = (b >> 3) & 0x3;

        std::uint16_t id;
        if (id16) {
            if (pos + 2 > bytes.size()) return false;
            id = static_cast<std::uint16_t>((static_cast<std::uint8_t>(bytes[pos]) << 8) |
                                            static_cast<std::uint8_t>(bytes[pos + 1]));
            pos += 2;
        } else {
            if (pos + 1 > bytes.size()) return false;
            id = static_cast<std::uint8_t>(bytes[pos]);
            pos += 1;
        }

        std::size_t len = 0;
        if (lenbytes == 1) {
            if (pos + 1 > bytes.size()) return false;
            len = static_cast<std::uint8_t>(bytes[pos]);
            pos += 1;
        } else if (lenbytes == 2) {
            if (pos + 2 > bytes.size()) return false;
            len = (static_cast<std::uint8_t>(bytes[pos]) << 8) |
                  static_cast<std::uint8_t>(bytes[pos + 1]);
            pos += 2;
        } else if (lenbytes == 3) {
            if (pos + 3 > bytes.size()) return false;
            len = (static_cast<std::uint8_t>(bytes[pos]) << 16) |
                  (static_cast<std::uint8_t>(bytes[pos + 1]) << 8) |
                  static_cast<std::uint8_t>(bytes[pos + 2]);
            pos += 3;
        }

        if (pos + len > bytes.size()) return false;

        Record r;
        r.type = type;
        r.id = id;
        r.value.assign(bytes, pos, len);
        pos += len;
        out.push_back(std::move(r));
    }
    return true;
}

std::vector<Record> resources(const std::vector<Record>& wire) {
    std::vector<Record> out;
    for (const auto& r : wire) {
        if (r.type == TYPE_RESOURCE) {
            out.push_back(r);
        } else if (r.type == TYPE_OBJECT_INSTANCE || r.type == TYPE_MULTIPLE_RESOURCE) {
            std::vector<Record> sub;
            if (decode(r.value, sub)) {
                auto leafs = resources(sub);
                out.insert(out.end(), leafs.begin(), leafs.end());
            }
        }
    }
    return out;
}

} // namespace mihini::lwm2m::tlv
