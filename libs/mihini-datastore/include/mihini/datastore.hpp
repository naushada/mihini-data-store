#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mihini/value.hpp"

namespace mihini {

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------
// RAII handle for a notification subscription. Move-only: moving transfers the
// subscription, destroying (or resetting) the last owner unregisters it.
class Subscription {
public:
    Subscription() = default;
    Subscription(Subscription&& other) noexcept = default;
    Subscription& operator=(Subscription&& other) noexcept = default;

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    // Unregisters the subscription, if still active.
    void reset();

    // True while the subscription is active (not yet unregistered).
    explicit operator bool() const { return id_ != 0; }

    // Internal: build a subscription backed by a DataStore unregister call.
    Subscription(std::function<void(std::uint64_t)> unregister, std::uint64_t id)
        : unregister_(std::move(unregister)), id_(id)
    {
    }

    ~Subscription() { reset(); }

private:
    std::function<void(std::uint64_t)> unregister_;
    std::uint64_t id_ = 0;
};

// ---------------------------------------------------------------------------
// DataStore
// ---------------------------------------------------------------------------
// C++ facade over a Mihini device-tree data store. It exposes the four things an
// application needs:
//
//   get      - read a variable (or a set of variables, or a node's children)
//   set      - write one or several variables
//   register - subscribe to change notifications, giving a notify callback
//   unregister - cancel a subscription
//
// The actual transport lives behind a Backend so the same facade runs against an
// in-process store (tests, offline apps) or the real EMP device tree.
class DataStore {
public:
    // Creates a store using the given backend. The backend must outlive the
    // store. See datastore.cpp for the factory helpers (make_inmemory, make_emp).
    explicit DataStore(class Backend& backend);
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;
    DataStore(DataStore&&) noexcept = default;
    DataStore& operator=(DataStore&&) noexcept = default;
    ~DataStore();

    // Establishes / tears down the transport. connect() is implicitly performed
    // by the backend factory when it is available.
    void connect();
    void disconnect();

    // --- get ----------------------------------------------------------------
    // Reads a single path. If the path is a leaf, returns its value; if it is a
    // node (non-leaf), throws DataStoreError. Use getChildren for nodes.
    Value get(const std::string& path) const;

    // Returns the direct children of a node path, in unspecified order.
    std::vector<std::string> getChildren(const std::string& path) const;

    // Reads several paths in one request. Returns only the leaf values; non-leaf
    // paths in the request are skipped (their children are not enumerated here).
    ChangeMap get(const std::vector<std::string>& paths) const;

    // --- set ----------------------------------------------------------------
    // Writes one variable.
    void set(const std::string& path, const Value& value);

    // Writes several variables in one operation.
    void set(const ChangeMap& values);

    // --- register / notify ---------------------------------------------------
    // Registers a notify callback for changes to `regVars`. Every time one of
    // them changes, `callback` is invoked with a ChangeMap containing all changed
    // registered variables plus every `passiveVars` entry (whether it changed or
    // not). A regVar may be a full path or a prefix denoting every variable
    // below it. Returns a subscription id for unregister().
    std::uint64_t registerNotify(const std::vector<std::string>& regVars,
                                 NotifyCallback callback,
                                 const std::vector<std::string>& passiveVars = {});

    // Cancels a subscription previously created by registerNotify(). No-op for an
    // unknown id.
    void unregister(std::uint64_t subscriptionId);

    // Convenience: registers a notify callback and wraps the id in an RAII
    // Subscription that unregisters itself on destruction.
    Subscription subscribe(const std::vector<std::string>& regVars,
                           NotifyCallback callback,
                           const std::vector<std::string>& passiveVars = {});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mihini
