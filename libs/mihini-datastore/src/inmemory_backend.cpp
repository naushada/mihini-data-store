#include "inmemory_backend.hpp"

#include <set>

namespace mihini {

namespace {

// Returns true when `key` lies strictly below `path` (a direct or indirect
// descendant), i.e. key == "path.<something>".
bool isDescendant(const std::string& key, const std::string& path)
{
    return key.size() > path.size() + 1 && key.compare(0, path.size(), path) == 0 &&
           key[path.size()] == '.';
}

} // namespace

void InMemoryBackend::connect()
{
    // Nothing to do for an in-process store.
}

void InMemoryBackend::disconnect()
{
    // Nothing to do for an in-process store.
}

bool InMemoryBackend::isNode(const std::string& path) const
{
    for (const auto& [key, value] : store_) {
        (void)value;
        if (isDescendant(key, path)) {
            return true;
        }
    }
    return false;
}

bool InMemoryBackend::matches(const std::string& path, const std::string& var)
{
    return path == var || isDescendant(path, var);
}

Value InMemoryBackend::get(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = store_.find(path);
    if (it != store_.end()) {
        return it->second;
    }
    if (isNode(path)) {
        return Value{}; // monostate == a node, mirroring a nil leaf get.
    }
    throw DataStoreError("path not found: " + path);
}

std::vector<std::string> InMemoryBackend::getChildren(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (store_.find(path) != store_.end()) {
        throw DataStoreError("path is a leaf, not a node: " + path);
    }
    if (!isNode(path)) {
        throw DataStoreError("path not found: " + path);
    }

    std::set<std::string> children;
    const std::size_t prefixLen = path.size() + 1; // include the '.'
    for (const auto& [key, value] : store_) {
        (void)value;
        if (!isDescendant(key, path)) {
            continue;
        }
        // Direct child = path + "." + first remaining component.
        const auto dot = key.find('.', prefixLen);
        children.insert(key.substr(0, dot));
    }
    return {children.begin(), children.end()};
}

ChangeMap InMemoryBackend::get(const std::vector<std::string>& paths)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ChangeMap result;
    for (const auto& path : paths) {
        const auto it = store_.find(path);
        if (it != store_.end()) {
            result[path] = it->second; // leaf value only; nodes are skipped.
        }
    }
    return result;
}

void InMemoryBackend::set(const std::string& path, const Value& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(path);
    const bool changed = (it == store_.end()) || (it->second != value);
    store_[path] = value;
    if (changed) {
        notifyChanged({path});
    }
}

void InMemoryBackend::set(const ChangeMap& values)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> changed;
    for (const auto& [path, value] : values) {
        auto it = store_.find(path);
        const bool changedPath = (it == store_.end()) || (it->second != value);
        store_[path] = value;
        if (changedPath) {
            changed.push_back(path);
        }
    }
    if (!changed.empty()) {
        notifyChanged(changed);
    }
}

std::uint64_t InMemoryBackend::registerNotify(const std::vector<std::string>& regVars,
                                              NotifyCallback callback,
                                              const std::vector<std::string>& passiveVars)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t id = nextId_++;
    subs_.push_back(Subscription{id, regVars, passiveVars, std::move(callback)});
    return id;
}

void InMemoryBackend::unregister(std::uint64_t subscriptionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = subs_.begin(); it != subs_.end(); ++it) {
        if (it->id == subscriptionId) {
            subs_.erase(it);
            return;
        }
    }
}

void InMemoryBackend::notifyChanged(const std::vector<std::string>& changed)
{
    // Callers hold mutex_. Callbacks are invoked synchronously (the EMP backend
    // differs: swi_dt_NotifyCB_t runs on its own thread).
    for (const auto& sub : subs_) {
        ChangeMap payload;
        bool triggered = false;
        for (const auto& path : changed) {
            for (const auto& regVar : sub.regVars) {
                if (matches(path, regVar)) {
                    payload[path] = store_[path];
                    triggered = true;
                    break;
                }
            }
        }
        if (!triggered) {
            continue;
        }
        for (const auto& passive : sub.passiveVars) {
            auto it = store_.find(passive);
            payload[passive] = (it != store_.end()) ? it->second : Value{};
        }
        if (sub.callback) {
            sub.callback(payload);
        }
    }
}

std::unique_ptr<Backend> make_inmemory_backend()
{
    return std::make_unique<InMemoryBackend>();
}

} // namespace mihini
