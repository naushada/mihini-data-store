#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "mihini/lwm2m/coap.hpp"
#include "mihini/lwm2m/dtls.hpp"
#include "mihini/lwm2m/engine.hpp"
#include "mihini/lwm2m/model.hpp"

using namespace mihini::lwm2m;

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

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

int main() {
    Engine engine = make_engine();

    DtlsAdapter server;
    DtlsAdapter client;

    CHECK(server.open(0));
    CHECK(client.open(0));
    const std::uint16_t sport = bound_port(server.fd());
    CHECK(sport != 0);

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

    CHECK(client.connect("127.0.0.1", sport));

    bool sent = false;
    bool ok = false;
    for (int i = 0; i < 2000 && !ok; ++i) {
        client.check_retransmit();
        server.check_retransmit();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client.fd(), &rfds);
        FD_SET(server.fd(), &rfds);
        const int nfds = (client.fd() > server.fd() ? client.fd() : server.fd()) + 1;
        struct timeval tv{0, 10000}; // 10 ms
        const int n = ::select(nfds, &rfds, nullptr, nullptr, &tv);
        if (n < 0) break;
        if (n > 0) {
            if (FD_ISSET(client.fd(), &rfds)) client.rx();
            if (FD_ISSET(server.fd(), &rfds)) server.rx();
        }

        if (client.connected() && !sent) {
            const std::string wire = coap::encode(make_get());
            client.tx_peer(wire);
            sent = true;
        }
        if (!client_payload.empty()) ok = true;
    }

    CHECK(client.connected());
    CHECK(client_payload == "42");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all dtls tests passed\n");
    return 0;
}
