#pragma once

#include <cstdint>
#include <string>
#include <vector>

// OMA LwM2M TLV codec (Core spec Annex C). Reference shape from the iot repo's
// lwm2m_codec_tlv.hpp. Handles the record framing: type field (bits 7-6),
// identifier length (bit 5), length-field size (bits 4-3), id, length, value.

namespace lwm2m::tlv {

// Type field (bits 7-6 of the first byte).
enum : std::uint8_t {
    TYPE_OBJECT_INSTANCE = 0,    // value = concatenated resource records
    TYPE_RESOURCE_INSTANCE = 1,  // value = one resource-instance value
    TYPE_MULTIPLE_RESOURCE = 2,  // value = concatenated resource-instance records
    TYPE_RESOURCE = 3,           // resource with value
};

struct Record {
    std::uint8_t type{0};
    std::uint16_t id{0};
    std::string value;
};

// Encode one TLV record.
std::string encode(std::uint8_t type, std::uint16_t id, const std::string& value);

// Convenience: a single resource-with-value record.
inline std::string encode_resource(std::uint16_t rid, const std::string& value) {
    return encode(TYPE_RESOURCE, rid, value);
}

// Encode an object-instance container holding the given resource records.
std::string encode_instance(std::uint16_t iid, const std::vector<Record>& resources);

// Decode a TLV stream into flat wire records (containers keep their raw value).
// Returns false on malformed input.
bool decode(const std::string& bytes, std::vector<Record>& out);

// Flatten wire records to resource-with-value leafs, recursing into object-
// instance (0) and multiple-resource (2) containers. Each leaf's `id` is its rid.
std::vector<Record> resources(const std::vector<Record>& wire);

} // namespace lwm2m::tlv
