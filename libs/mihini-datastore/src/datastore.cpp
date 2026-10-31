#include "mihini/datastore.hpp"

#include <utility>

#include "mihini/backend.hpp"

namespace mihini {

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------
void Subscription::reset()
{
    if (id_ != 0) {
        if (unregister_) {
            unregister_(id_);
        }
        id_ = 0;
        unregister_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// DataStore::Impl
// ---------------------------------------------------------------------------
class DataStore::Impl {
public:
    explicit Impl(Backend& backend)
        : backend_(backend)
    {
    }

    void connect() { backend_.connect(); }
    void disconnect() { backend_.disconnect(); }

    Value get(const std::string& path) const { return backend_.get(path); }

    std::vector<std::string> getChildren(const std::string& path) const
    {
        return backend_.getChildren(path);
    }

    ChangeMap get(const std::vector<std::string>& paths) const
    {
        return backend_.get(paths);
    }

    void set(const std::string& path, const Value& value)
    {
        backend_.set(path, value);
    }

    void set(const ChangeMap& values) { backend_.set(values); }

    std::uint64_t registerNotify(const std::vector<std::string>& regVars,
                                 NotifyCallback callback,
                                 const std::vector<std::string>& passiveVars)
    {
        return backend_.registerNotify(regVars, std::move(callback), passiveVars);
    }

    void unregister(std::uint64_t subscriptionId)
    {
        backend_.unregister(subscriptionId);
    }

private:
    Backend& backend_;
};

// ---------------------------------------------------------------------------
// DataStore
// ---------------------------------------------------------------------------
DataStore::DataStore(Backend& backend)
    : impl_(std::make_unique<Impl>(backend))
{
}

DataStore::~DataStore() = default;

void DataStore::connect() { impl_->connect(); }

void DataStore::disconnect() { impl_->disconnect(); }

Value DataStore::get(const std::string& path) const { return impl_->get(path); }

std::vector<std::string> DataStore::getChildren(const std::string& path) const
{
    return impl_->getChildren(path);
}

ChangeMap DataStore::get(const std::vector<std::string>& paths) const
{
    return impl_->get(paths);
}

void DataStore::set(const std::string& path, const Value& value)
{
    impl_->set(path, value);
}

void DataStore::set(const ChangeMap& values) { impl_->set(values); }

std::uint64_t DataStore::registerNotify(const std::vector<std::string>& regVars,
                                        NotifyCallback callback,
                                        const std::vector<std::string>& passiveVars)
{
    return impl_->registerNotify(regVars, std::move(callback), passiveVars);
}

void DataStore::unregister(std::uint64_t subscriptionId)
{
    impl_->unregister(subscriptionId);
}

Subscription DataStore::subscribe(const std::vector<std::string>& regVars,
                                  NotifyCallback callback,
                                  const std::vector<std::string>& passiveVars)
{
    auto unregister = [this](std::uint64_t id) { this->unregister(id); };
    const std::uint64_t id = registerNotify(regVars, std::move(callback), passiveVars);
    return Subscription(std::move(unregister), id);
}

} // namespace mihini
