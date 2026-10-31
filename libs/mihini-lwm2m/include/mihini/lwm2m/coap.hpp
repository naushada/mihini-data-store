#pragma once

#include <cstdint>
#include <string>
#include <vector>

// CoAP (RFC 7252) message model and wire codec -- the transport under LwM2M.
// Self-contained, no external dependencies. Reference implementation shape from
// the iot repo's coap_adapter.hpp, reduced to what read/write/push needs.

namespace mihini::lwm2m::coap {

enum class Type : std::uint8_t {
    Con = 0,   // confirmable
    Non = 1,   // non-confirmable
    Ack = 2,   // acknowledgement
    Rst = 3,   // reset
};

// CoAP codes (class.detail packed into one byte). LwM2M uses only a handful.
enum class Code : std::uint8_t {
    Empty = 0x00,
    Get = 0x01,
    Post = 0x02,
    Put = 0x03,
    Delete = 0x04,
    Created = 0x41,              // 2.01
    Changed = 0x44,              // 2.04
    Content = 0x45,              // 2.05
    BadRequest = 0x80,           // 4.00
    NotFound = 0x84,             // 4.04
    MethodNotAllowed = 0x85,     // 4.05
    InternalServerError = 0xA0,  // 5.00
};

// Well-known option numbers.
constexpr std::uint16_t OPT_IF_MATCH = 1;
constexpr std::uint16_t OPT_URI_HOST = 3;
constexpr std::uint16_t OPT_ETAG = 4;
constexpr std::uint16_t OPT_IF_NONE_MATCH = 5;
constexpr std::uint16_t OPT_OBSERVE = 6;
constexpr std::uint16_t OPT_URI_PORT = 7;
constexpr std::uint16_t OPT_LOCATION_PATH = 8;
constexpr std::uint16_t OPT_URI_PATH = 11;
constexpr std::uint16_t OPT_CONTENT_FORMAT = 12;
constexpr std::uint16_t OPT_MAX_AGE = 14;
constexpr std::uint16_t OPT_URI_QUERY = 15;
constexpr std::uint16_t OPT_ACCEPT = 17;

// LwM2M content formats.
constexpr std::uint16_t CF_TEXT = 0;              // text/plain
constexpr std::uint16_t CF_LINK_FORMAT = 40;      // application/link-format
constexpr std::uint16_t CF_OCTET_STREAM = 42;     // application/octet-stream
constexpr std::uint16_t CF_SENML_JSON = 110;      // application/senml+json
constexpr std::uint16_t CF_SENML_CBOR = 112;      // application/senml+cbor
constexpr std::uint16_t CF_TLV = 11542;           // application/vnd.oma.lwm2m+tlv

struct Option {
    std::uint16_t number{0};
    std::string value;
};

struct Message {
    Type type{Type::Con};
    Code code{Code::Get};
    std::uint16_t id{0};
    std::string token;               // 0..8 bytes
    std::vector<Option> options;
    std::string payload;

    // First option with the given number, or nullptr.
    const std::string* find_option(std::uint16_t number) const;

    // Reconstructed "/3/0/13" path from the Uri-Path options.
    std::string uri_path() const;

    // Reconstructed "k=v&k2=v2" query from the Uri-Query options.
    std::string uri_query() const;
};

// Encode a message to its wire representation.
std::string encode(const Message& m);

// Decode a wire message. Returns false on malformed input.
bool decode(const std::string& bytes, Message& m);

inline std::uint16_t code_class(Code c) { return static_cast<std::uint8_t>(c) >> 5; }
inline std::uint16_t code_detail(Code c) { return static_cast<std::uint8_t>(c) & 0x1f; }

} // namespace mihini::lwm2m::coap
