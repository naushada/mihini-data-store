#include <cstdint>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>

#include <event2/event.h>

#include <gtest/gtest.h>

#include "lwm2m/coap.hpp"
#include "lwm2m/dtls.hpp"
#include "lwm2m/engine.hpp"
#include "lwm2m/model.hpp"

using namespace lwm2m;

// TLS_PSK_WITH_AES_128_CCM_8 uses a 16-byte key; stored as hex like the iot repo.
static const std::string PSK_ID = "iot-client";
static const std::string PSK_HEX = "00112233445566778899aabbccddeeff";

static Engine make_engine() {
    Engine e;
    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;
    Resource r;
    r.rid = 13;
    r.type = ResourceType::Integer;
    r.ops = Operations::R;
    r.read = []() { return "42"; };
    inst.resources[13] = r;
    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));
    return e;
}

static coap::Message make_get() {
    coap::Message m;
    m.type = coap::Type::Con;
    m.code = coap::Code::Get;
    m.id = 0x4242;
    m.token = "dt";
    m.options.push_back({coap::OPT_URI_PATH, "3"});
    m.options.push_back({coap::OPT_URI_PATH, "0"});
    m.options.push_back({coap::OPT_URI_PATH, "13"});
    return m;
}

static std::uint16_t bound_port(int fd) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&sin), &len) < 0) return 0;
    return ntohs(sin.sin_port);
}

TEST(Dtls, PskHandshakeRoundtrip) {
    Engine engine = make_engine();

    DtlsAdapter server;
    DtlsAdapter client;

    ASSERT_TRUE(server.open(0));
    ASSERT_TRUE(client.open(0));
    const std::uint16_t sport = bound_port(server.fd());
    ASSERT_NE(sport, 0);

    // Shared PSK. The server resolves the key by the identity the client
    // presents (credentials map); the client pins its identity before connect.
    server.add_credential(PSK_ID, PSK_HEX);
    client.add_credential(PSK_ID, PSK_HEX);
    client.active_identity(PSK_ID);

    // Server: decrypt CoAP, dispatch through the Engine, send encoded responses.
    server.set_handler([&](const std::string& plaintext, const std::string&) {
        coap::Message req;
        if (!coap::decode(plaintext, req)) return std::vector<std::string>{};
        return engine.handle(req);
    });

    // Client: capture the decrypted CoAP response payload.
    std::string client_payload;
    client.set_handler([&](const std::string& plaintext, const std::string&) {
        coap::Message resp;
        if (coap::decode(plaintext, resp)) client_payload = resp.payload;
        return std::vector<std::string>{};
    });

    ASSERT_TRUE(client.connect("127.0.0.1", sport));

    // Drive both transports through their libevent bases until the handshake
    // completes and the client receives the response, or the deadline passes.
    bool sent = false;
    bool ok = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ok && std::chrono::steady_clock::now() < deadline) {
        event_base_loop(client.base(), EVLOOP_ONCE);
        event_base_loop(server.base(), EVLOOP_ONCE);

        if (client.connected() && !sent) {
            client.tx_peer(coap::encode(make_get()));
            sent = true;
        }
        if (!client_payload.empty()) ok = true;
    }

    EXPECT_TRUE(client.connected());
    EXPECT_EQ(client_payload, "42");
}
