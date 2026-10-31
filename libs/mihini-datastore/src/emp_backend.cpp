#include "emp_backend.hpp"

#ifdef MIHINI_DATSTORE_WITH_SWILIBS

#include <type_traits>

#include "swi_devicetree.h"
#include "swi_dset.h"

namespace mihini {

namespace {

// The swi_dt_Init()/swi_dt_Destroy() API is a process-wide singleton, so there is
// at most one live EmpBackend. The C notify callback carries no user-data pointer,
// so the trampoline routes through this global.
EmpBackend* g_activeBackend = nullptr;

void notifyTrampoline(swi_dset_Iterator_t* data)
{
    if (g_activeBackend != nullptr) {
        g_activeBackend->onNotify(data);
    }
}

// Converts the current iterator element into a Value.
Value fromIterator(swi_dset_Iterator_t* it)
{
    switch (swi_dset_GetType(it)) {
    case SWI_DSET_NIL:
        return Value{}; // monostate
    case SWI_DSET_INTEGER:
        return Value{static_cast<std::int64_t>(swi_dset_ToInteger(it))};
    case SWI_DSET_FLOAT:
        return Value{swi_dset_ToFloat(it)};
    case SWI_DSET_STRING: {
        const char* s = swi_dset_ToString(it);
        return Value{s != nullptr ? std::string(s) : std::string{}};
    }
    case SWI_DSET_BOOL:
        return Value{swi_dset_ToBool(it)};
    case SWI_DSET_UNSUPPORTED:
    default:
        throw DataStoreError("unsupported dset type");
    }
}

// Iterates the whole iterator, returning a name -> value map.
ChangeMap iteratorToMap(swi_dset_Iterator_t* it)
{
    ChangeMap result;
    for (;;) {
        const char* name = swi_dset_GetName(it);
        if (name == nullptr) {
            break;
        }
        result[std::string(name)] = fromIterator(it);
        if (swi_dset_Next(it) != SWI_STATUS_OK) {
            break;
        }
    }
    return result;
}

void checkStatus(swi_status_t status, const char* what)
{
    if (status != SWI_STATUS_OK) {
        throw DataStoreError(std::string(what) + " failed with swi status " +
                             std::to_string(static_cast<int>(status)));
    }
}

bool matchesAny(const ChangeMap& map, const std::vector<std::string>& regVars)
{
    for (const auto& [path, value] : map) {
        (void)value;
        for (const auto& var : regVars) {
            if (path == var ||
                (path.size() > var.size() + 1 &&
                 path.compare(0, var.size(), var) == 0 && path[var.size()] == '.')) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

void EmpBackend::connect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        return;
    }
    checkStatus(swi_dt_Init(), "swi_dt_Init");
    g_activeBackend = this;
    connected_ = true;
}

void EmpBackend::disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return;
    }
    g_activeBackend = nullptr;
    swi_dt_Destroy();
    connected_ = false;
}

Value EmpBackend::get(const std::string& path)
{
    swi_dset_Iterator_t* it = nullptr;
    const swi_status_t status = swi_dt_Get(path.c_str(), &it);
    if (status == SWI_STATUS_DA_NOT_FOUND) {
        throw DataStoreError("path not found: " + path);
    }
    if (status == SWI_STATUS_DA_NODE) {
        swi_dset_Destroy(it);
        return Value{}; // a node, mirrored as nil
    }
    checkStatus(status, "swi_dt_Get");
    Value value = fromIterator(it);
    swi_dset_Destroy(it);
    return value;
}

std::vector<std::string> EmpBackend::getChildren(const std::string& path)
{
    swi_dset_Iterator_t* it = nullptr;
    const swi_status_t status = swi_dt_Get(path.c_str(), &it);
    if (status == SWI_STATUS_DA_NOT_FOUND) {
        throw DataStoreError("path not found: " + path);
    }
    if (status == SWI_STATUS_OK) {
        swi_dset_Destroy(it);
        throw DataStoreError("path is a leaf, not a node: " + path);
    }
    if (status != SWI_STATUS_DA_NODE) {
        checkStatus(status, "swi_dt_Get");
    }

    std::vector<std::string> children;
    for (;;) {
        const char* name = swi_dset_GetName(it);
        if (name == nullptr) {
            break;
        }
        children.emplace_back(name);
        if (swi_dset_Next(it) != SWI_STATUS_OK) {
            break;
        }
    }
    swi_dset_Destroy(it);
    return children;
}

ChangeMap EmpBackend::get(const std::vector<std::string>& paths)
{
    if (paths.empty()) {
        return {};
    }
    std::vector<const char*> ptrs;
    ptrs.reserve(paths.size());
    for (const auto& path : paths) {
        ptrs.push_back(path.c_str());
    }
    swi_dset_Iterator_t* it = nullptr;
    const swi_status_t status = swi_dt_MultipleGet(ptrs.size(), ptrs.data(), &it);
    if (status == SWI_STATUS_DA_NOT_FOUND) {
        throw DataStoreError("one or more paths not found");
    }
    checkStatus(status, "swi_dt_MultipleGet");
    ChangeMap result = iteratorToMap(it);
    swi_dset_Destroy(it);
    return result;
}

void EmpBackend::set(const std::string& path, const Value& value)
{
    swi_status_t status = SWI_STATUS_OK;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                status = swi_dt_SetNull(path.c_str());
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                status = swi_dt_SetInteger(path.c_str(), static_cast<int>(v));
            } else if constexpr (std::is_same_v<T, double>) {
                status = swi_dt_SetFloat(path.c_str(), v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                status = swi_dt_SetString(path.c_str(), v.c_str());
            } else if constexpr (std::is_same_v<T, bool>) {
                status = swi_dt_SetBool(path.c_str(), v);
            }
        },
        value);
    checkStatus(status, "swi_dt_Set");
}

void EmpBackend::set(const ChangeMap& values)
{
    for (const auto& [path, value] : values) {
        set(path, value);
    }
}

std::uint64_t EmpBackend::registerNotify(const std::vector<std::string>& regVars,
                                         NotifyCallback callback,
                                         const std::vector<std::string>& passiveVars)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<const char*> regPtrs;
    regPtrs.reserve(regVars.size());
    for (const auto& v : regVars) {
        regPtrs.push_back(v.c_str());
    }
    std::vector<const char*> passivePtrs;
    passivePtrs.reserve(passiveVars.size());
    for (const auto& v : passiveVars) {
        passivePtrs.push_back(v.c_str());
    }

    void* regId = nullptr;
    const swi_status_t status = swi_dt_Register(regPtrs.size(), regPtrs.data(),
                                                notifyTrampoline, passivePtrs.size(),
                                                passivePtrs.data(), &regId);
    checkStatus(status, "swi_dt_Register");

    const std::uint64_t id = nextId_++;
    subs_.push_back(Subscription{id, regId, regVars, std::move(callback)});
    return id;
}

void EmpBackend::unregister(std::uint64_t subscriptionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = subs_.begin(); it != subs_.end(); ++it) {
        if (it->id == subscriptionId) {
            swi_dt_Unregister(it->regId);
            subs_.erase(it);
            return;
        }
    }
}

void EmpBackend::onNotify(void* dataIterator)
{
    auto* it = static_cast<swi_dset_Iterator_t*>(dataIterator);
    ChangeMap changes = iteratorToMap(it);

    // The swi_dt notify callback has no user-data pointer, so it cannot say which
    // registration fired. Fan out to every subscriber whose regVars intersect the
    // delivered change set.
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& sub : subs_) {
        if (matchesAny(changes, sub.regVars) && sub.callback) {
            sub.callback(changes);
        }
    }
}

} // namespace mihini

#endif // MIHINI_DATSTORE_WITH_SWILIBS

namespace mihini {

std::unique_ptr<Backend> make_emp_backend()
{
#ifdef MIHINI_DATSTORE_WITH_SWILIBS
    return std::make_unique<EmpBackend>();
#else
    throw DataStoreError(
        "EMP backend not available: build with -DMIHINI_DATSTORE_WITH_SWILIBS=ON "
        "and point the build at the swi_* client libraries");
#endif
}

} // namespace mihini
