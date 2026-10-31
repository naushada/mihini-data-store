#include "lwm2m/model.hpp"

namespace lwm2m {

void ObjectStore::add_object(ObjectDescriptor descriptor) {
    objects_[descriptor.oid] = std::move(descriptor);
}

bool ObjectStore::has(std::uint32_t oid) const {
    return objects_.count(oid) != 0;
}

bool ObjectStore::has(std::uint32_t oid, std::uint32_t iid) const {
    const auto* od = find(oid);
    return od && od->instances.count(iid) != 0;
}

bool ObjectStore::has(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid) const {
    const auto* inst = find(oid, iid);
    return inst && inst->resources.count(rid) != 0;
}

ObjectDescriptor* ObjectStore::find(std::uint32_t oid) {
    auto it = objects_.find(oid);
    return it == objects_.end() ? nullptr : &it->second;
}

ObjectInstance* ObjectStore::find(std::uint32_t oid, std::uint32_t iid) {
    auto* od = find(oid);
    if (!od) return nullptr;
    auto it = od->instances.find(iid);
    return it == od->instances.end() ? nullptr : &it->second;
}

Resource* ObjectStore::find(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid) {
    auto* inst = find(oid, iid);
    if (!inst) return nullptr;
    auto it = inst->resources.find(rid);
    return it == inst->resources.end() ? nullptr : &it->second;
}

const ObjectDescriptor* ObjectStore::find(std::uint32_t oid) const {
    auto it = objects_.find(oid);
    return it == objects_.end() ? nullptr : &it->second;
}

const ObjectInstance* ObjectStore::find(std::uint32_t oid, std::uint32_t iid) const {
    const auto* od = find(oid);
    if (!od) return nullptr;
    auto it = od->instances.find(iid);
    return it == od->instances.end() ? nullptr : &it->second;
}

const Resource* ObjectStore::find(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid) const {
    const auto* inst = find(oid, iid);
    if (!inst) return nullptr;
    auto it = inst->resources.find(rid);
    return it == inst->resources.end() ? nullptr : &it->second;
}

} // namespace lwm2m
