#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "mihini/backend.hpp"
#include "mihini/value.hpp"

namespace mihini {

// ---------------------------------------------------------------------------
// InMemoryBackend
// ---------------------------------------------------------------------------
// Offline backend that keeps the whole tree in a flat std::map. Paths use the
// same dot-separated convention as the Mihini device tree
// ("system.sw_info.fw_ver"), so code written against this backend ports to the
// EMP backend unchanged.
class InMemoryBackend : public Backend {
public:
    void connect() override;
    void disconnect() override;

    Value get(const std::string& path) override;
    std::vector<std::string> getChildren(const std::string& path) override;
    ChangeMap get(const std::vector<std::string>& paths) override;

    void set(const std::string& path, const Value& value) override;
    void set(const ChangeMap& values) override;

    std::uint64_t registerNotify(const std::vector<std::string>& regVars,
                                 NotifyCallback callback,
                                 const std::vector<std::string>& passiveVars) override;
    void unregister(std::uint64_t subscriptionId) override;

private:
    struct Subscription {
        std::uint64_t id = 0;
        std::vector<std::string> regVars;
        std::vector<std::string> passiveVars;
        NotifyCallback callback;
    };

    // True when `path` is a non-leaf node, i.e. some key lives strictly below it.
    bool isNode(const std::string& path) const;

    // True when `path` equals `var` or is a descendant of `var` ("var." prefix).
    static bool matches(const std::string& path, const std::string& var);

    void notifyChanged(const std::vector<std::string>& changed);

    std::map<std::string, Value> store_;
    std::vector<Subscription> subs_;
    std::uint64_t nextId_ = 1;
    mutable std::mutex mutex_;
};

} // namespace mihini
