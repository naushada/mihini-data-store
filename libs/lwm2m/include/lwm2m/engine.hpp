#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "coap.hpp"
#include "model.hpp"
#include "senml.hpp"

// LwM2M 1.1 method dispatcher: Read, Write, Execute, and Push (Observe/Notify +
// Send). Pure logic over the ObjectStore -- no sockets. Reference shape from the
// iot repo's lwm2m_dm_client.hpp + lwm2m_observe.hpp.

namespace lwm2m {

class Engine {
public:
    ObjectStore& store() { return store_; }
    const ObjectStore& store() const { return store_; }

    // Handle one inbound CoAP request; returns the wire response frames to send.
    // For an Observe register this is the initial 2.05 (Observe: 0) frame.
    std::vector<std::string> handle(const coap::Message& req);

    // Push (Observe/Notify): emit Notify frames for every observer on
    // (oid,iid,rid). Called after a write/execute changes a resource.
    std::vector<std::string> notify(std::uint32_t oid, std::uint32_t iid, std::uint32_t rid);

    // Push (Send): build a LwM2M 1.1 Send request (POST /dp, SenML JSON) for the
    // given records. The caller ships it and correlates the 2.04 by message id.
    static std::string build_send(std::uint16_t msgid, const std::string& token,
                                  const std::vector<senml::Record>& records);

private:
    struct Observer {
        std::string token;
        std::uint32_t oid{0};
        std::uint32_t iid{0};
        std::uint32_t rid{0};
        std::uint32_t seq{0};
    };

    ObjectStore store_;
    std::vector<Observer> observers_;

    Observer* find_observer(const std::string& token);

    std::vector<std::string> handle_read(const coap::Message& req,
                                         const std::vector<std::int32_t>& seg,
                                         bool observe);
    std::vector<std::string> handle_write(const coap::Message& req,
                                          const std::vector<std::int32_t>& seg);
    std::vector<std::string> handle_post(const coap::Message& req,
                                         const std::vector<std::int32_t>& seg);
};

} // namespace lwm2m
