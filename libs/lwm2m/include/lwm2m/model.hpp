#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

// Runtime LwM2M object/resource model. Reference shape from the iot repo's
// lwm2m_object_store.hpp. Resources expose read/write/execute as std::function
// so concrete bindings (device info, app data) can be injected without touching
// the codec or dispatch layers.

namespace lwm2m {

// OMA LwM2M Core 1.1 §C data types.
enum class ResourceType : std::uint8_t {
    None,
    String,
    Integer,
    Float,
    Boolean,
    Opaque,
    Time,
    ObjLink,
};

// Resource operations from the OMA registry.
enum class Operations : std::uint8_t {
    None = 0,
    R = 1 << 0,
    W = 1 << 1,
    E = 1 << 2,
    RW = R | W,
    RWE = R | W | E,
};

constexpr Operations operator|(Operations a, Operations b) {
    return static_cast<Operations>(static_cast<std::uint8_t>(a) |
                                   static_cast<std::uint8_t>(b));
}
constexpr bool has_op(Operations set, Operations op) {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(op)) != 0;
}

// One LwM2M Resource.
struct Resource {
    std::uint32_t rid{0};
    std::string name;
    ResourceType type{ResourceType::None};
    Operations ops{Operations::None};
    bool observable{false};

    // Value as text (e.g. "false", "42", "hello"). Empty function = unsupported.
    std::function<std::string()> read;
    // Accepts a decoded text value; returns 0 on success or a CoAP 4.xx/5.xx code.
    std::function<int(const std::string&)> write;
    // Execute (POST /{oid}/{iid}/{rid}); returns 0 or a CoAP error code.
    std::function<int(const std::string&)> execute;
};

struct ObjectInstance {
    std::uint32_t iid{0};
    std::unordered_map<std::uint32_t, Resource> resources;  // keyed by rid
};

struct ObjectDescriptor {
    std::uint32_t oid{0};
    std::string name;
    std::map<std::uint32_t, ObjectInstance> instances;      // ascending iid
};

// The runtime object/resource database for one LwM2M endpoint.
class ObjectStore {
public:
    void add_object(ObjectDescriptor descriptor);

    bool has(std::uint32_t oid) const;
    bool has(std::uint32_t oid, std::uint32_t iid) const;
    bool has(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid) const;

    ObjectDescriptor* find(std::uint32_t oid);
    ObjectInstance* find(std::uint32_t oid, std::uint32_t iid);
    Resource* find(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid);

    const ObjectDescriptor* find(std::uint32_t oid) const;
    const ObjectInstance* find(std::uint32_t oid, std::uint32_t iid) const;
    const Resource* find(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid) const;

    const std::map<std::uint32_t, ObjectDescriptor>& objects() const { return objects_; }

private:
    std::map<std::uint32_t, ObjectDescriptor> objects_;
};

} // namespace lwm2m
