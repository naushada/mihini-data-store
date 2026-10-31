#pragma once

#include <cstdint>
#include <string>
#include <vector>

// SenML JSON codec (RFC 8428), the LwM2M 1.1 content format for Observe/Notify
// payloads and the Send ("push") operation. Reference shape from the iot repo's
// lwm2m_codec_senml.hpp. Covers the LwM2M-relevant subset: bn, n, v, vs, vb, vd.

namespace mihini::lwm2m::senml {

enum class Kind : std::uint8_t {
    None,
    Numeric,
    String,
    Boolean,
    Data,      // vd: opaque bytes, base64url on JSON
};

struct Record {
    std::string baseName;   // bn
    std::string name;       // n

    Kind kind{Kind::None};
    double numericValue{0.0};
    bool isFloat{false};
    std::string stringValue;
    bool booleanValue{false};
    std::string dataValue;  // raw bytes (encoded as base64url on JSON)

    // Full LwM2M URI, e.g. "/3/0/13".
    std::string path() const { return baseName + name; }
};

// Encode a record list as SenML JSON. Returns an empty string if two records
// disagree on `baseName` (RFC 8428 §4.5: bn is carried by the first record only).
std::string encode_json(const std::vector<Record>& records);

// Decode SenML JSON. Returns false on malformed input.
bool decode_json(const std::string& bytes, std::vector<Record>& out);

} // namespace mihini::lwm2m::senml
