#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mihini/value.hpp"

namespace mihini {

// ---------------------------------------------------------------------------
// Backend
// ---------------------------------------------------------------------------
// Transport seam behind DataStore. A backend owns the mechanics of reaching the
// device tree (in-process map, EMP over the swi_* C client, ...) and DataStore
// owns argument checking and error mapping. Apps can also implement their own.
class Backend {
public:
    virtual ~Backend() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;

    // get -------------------------------------------------------------------
    // Returns the value of a leaf path. Returns std::monostate for a node.
    // Throws DataStoreError on not-found or other transport errors.
    virtual Value get(const std::string& path) = 0;

    // Returns the direct children of a node path. Throws DataStoreError if the
    // path is a leaf or not found.
    virtual std::vector<std::string> getChildren(const std::string& path) = 0;

    virtual ChangeMap get(const std::vector<std::string>& paths) = 0;

    // set -------------------------------------------------------------------
    virtual void set(const std::string& path, const Value& value) = 0;
    virtual void set(const ChangeMap& values) = 0;

    // register / notify -------------------------------------------------------
    virtual std::uint64_t registerNotify(const std::vector<std::string>& regVars,
                                         NotifyCallback callback,
                                         const std::vector<std::string>& passiveVars) = 0;
    virtual void unregister(std::uint64_t subscriptionId) = 0;
};

// Factory for the offline, in-process backend.
std::unique_ptr<Backend> make_inmemory_backend();

// Factory for the EMP backend (wraps the swi_devicetree/swi_dset C client).
// When the library is built without MIHINI_DATSTORE_WITH_SWILIBS, this still
// links but throws DataStoreError at call time.
std::unique_ptr<Backend> make_emp_backend();

} // namespace mihini
