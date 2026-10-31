#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// DTLS 1.2 transport over tinydtls — the CoAPs wire under LwM2M. Adapted from
// the iot repo's DTLSAdapter (apps/src/dtls_adapter.cpp) with the ACE logging
// and data-store coupling removed: PSK lookup is a std::function and decrypted
// CoAP is handed to an InboundHandler the caller wires to mihini::lwm2m::Engine.
//
// The adapter owns the UDP socket and the tinydtls context but performs no I/O
// loop of its own; the host drives rx() (datagrams in) and check_retransmit()
// (handshake retransmission timer) exactly like the iot repo's reactor tick.
//
// tinydtls is vendored under 3rdparty/ and kept entirely out of this header:
// DtlsImpl is an opaque handle defined in src/dtls.cpp, so codec-only consumers
// of mihini-lwm2m never see the C headers.

namespace mihini::lwm2m {

struct DtlsImpl; // opaque; defined in src/dtls.cpp

class DtlsAdapter {
public:
    // Decrypted application data (one CoAP frame) from `peer` (an "ip:port"
    // string); returns the plaintext response frames to send back over that
    // same DTLS session.
    using InboundHandler = std::function<std::vector<std::string>(
        const std::string& plaintext, const std::string& peer)>;

    // Server-role PSK lookup: identity -> hex secret ("" when unknown).
    using PskResolver = std::function<std::string(const std::string& identity)>;

    DtlsAdapter();
    ~DtlsAdapter();

    DtlsAdapter(DtlsAdapter&&) noexcept;
    DtlsAdapter& operator=(DtlsAdapter&&) noexcept;
    DtlsAdapter(const DtlsAdapter&) = delete;
    DtlsAdapter& operator=(const DtlsAdapter&) = delete;

    // Bind the UDP socket. `port` 0 picks an ephemeral port. Returns false on
    // error (e.g. address in use).
    bool open(std::uint16_t port);
    void close();

    int fd() const;

    // Client role: pin the peer and start a DTLS handshake (dtls_connect). The
    // initial ClientHello is emitted through the write callback.
    bool connect(const std::string& host, std::uint16_t port);

    // True once the handshake for the current session completed.
    bool connected() const;

    void set_handler(InboundHandler h);
    void set_psk_resolver(PskResolver r);

    // Install an identity -> hex secret. The first credential added pins the
    // bootstrap identity.
    void add_credential(const std::string& identity, const std::string& hex_secret);

    // Pin the PSK identity presented on the next handshake (client role).
    void active_identity(const std::string& id);
    std::string active_identity() const;

    // Read one datagram, feed tinydtls, and flush any handler responses back to
    // the peer. Returns the dtls_handle_message() result (0 on success).
    int rx();

    // Send one plaintext CoAP frame to the last inbound peer (server reply).
    int tx(const std::string& wire);
    // Send one plaintext CoAP frame to the pinned connect target (client).
    int tx_peer(const std::string& wire);

    // Pump tinydtls' handshake retransmission timer. Returns true when tinydtls
    // has exhausted its max retransmits for some peer (handshake gave up).
    bool check_retransmit();

private:
    std::unique_ptr<DtlsImpl> impl_;
};

} // namespace mihini::lwm2m
