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
// EmpBackend
// ---------------------------------------------------------------------------
// Backend over the real Mihini device tree, via the `swi_devicetree` /
// `swi_dset` C client. Only compiled when MIHINI_DATSTORE_WITH_SWILIBS is set;
// otherwise make_emp_backend() still links but throws at call time.
//
// The swi_* API sends EMP commands (GetVariable, SetVariable, RegisterVariable,
// NotifyVariable, UnregisterVariable) to the agent's device tree.
class EmpBackend : public Backend {
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

    // Invoked on the notify pthread from the swi_dt_NotifyCB_t trampoline with a
    // swi_dset_Iterator_t*. Converts it and fans out to matching subscribers.
    void onNotify(void* dataIterator);

private:
    struct Subscription {
        std::uint64_t id = 0;
        void* regId = nullptr; // swi_dt_regId_t
        std::vector<std::string> regVars;
        NotifyCallback callback;
    };

    std::vector<Subscription> subs_;
    std::uint64_t nextId_ = 1;
    std::mutex mutex_;
    bool connected_ = false;
};

} // namespace mihini
