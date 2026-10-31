#include "mihini/lwm2m/dtls.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <map>

// tinydtls is a C library; pull its headers under C linkage. numeric.h #defines
// max/min macros, so this TU must avoid a bare max(...)/min(...) call (std::* is
// unaffected).
extern "C" {
#include "tinydtls.h"
#include "dtls.h"
#include "dtls_debug.h"
#include "session.h"
#include "alert.h"
}

namespace mihini::lwm2m {

namespace {

std::string session_key(const session_t& s) {
    struct in_addr a;
    a.s_addr = s.addr.sin.sin_addr.s_addr;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s:%u", ::inet_ntoa(a),
                  static_cast<unsigned>(ntohs(s.addr.sin.sin_port)));
    return std::string(buf);
}

std::string hex_to_binary(const std::string& hex) {
    if (hex.size() % 2 != 0) return {};
    std::string out;
    out.reserve(hex.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

void set_session(session_t& s, const std::string& host, std::uint16_t port) {
    std::memset(&s, 0, sizeof(s));

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* result = nullptr;

    if (::getaddrinfo(host.c_str(), nullptr, &hints, &result) == 0 && result) {
        s.addr.sin.sin_addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr)->sin_addr;
        ::freeaddrinfo(result);
    } else {
        s.addr.sin.sin_addr.s_addr = ::inet_addr(host.c_str());
    }

    s.addr.sin.sin_family = AF_INET;
    s.addr.sin.sin_port = htons(port);
    std::memset(s.addr.sin.sin_zero, 0, sizeof(s.addr.sin.sin_zero));
#ifdef __APPLE__
    // macOS/BSD fill sockaddr_in.sin_len with 16 in recvfrom(), but this
    // hand-built session would leave it 0. tinydtls keys its peer hash on the
    // raw sizeof(session_t) bytes (FIND_PEER/ADD_PEER -> uthash memcmp), so the
    // byte must match the inbound recvfrom() session or the client never finds
    // its peer again after the HelloVerifyRequest. Match recvfrom() exactly.
    s.addr.sin.sin_len = sizeof(struct sockaddr_in);
#endif
    s.size = sizeof(struct sockaddr_in);
}

} // namespace

// All tinydtls-facing state. Members are public so the extern "C" callbacks
// below (which receive this via dtls_get_app_data) can reach them directly.
struct DtlsImpl {
    dtls_context_t* ctx = nullptr;
    dtls_handler_t cb{};
    int fd = -1;
    session_t session{};      // last inbound peer (server reply target)
    session_t peer{};         // pinned connect target (client)
    bool is_client = false;
    bool connected = false;
    std::string active_identity;
    std::string bootstrap_identity;
    std::map<std::string, std::string> credentials;  // identity -> hex secret
    DtlsAdapter::PskResolver resolver;
    DtlsAdapter::InboundHandler handler;
    std::vector<std::string> responses;

    std::string resolve_secret(const std::string& identity) const {
        auto it = credentials.find(identity);
        if (it != credentials.end()) return it->second;
        return resolver ? resolver(identity) : std::string();
    }

    std::string presented_identity() const {
        if (!active_identity.empty()) return active_identity;
        if (is_client && !credentials.empty()) return credentials.begin()->first;
        return std::string();
    }
};

} // namespace mihini::lwm2m

// The callbacks are assigned into dtls_handler_t's C function-pointer fields, so
// they need C language linkage (see the iot repo's dtls_adapter.hpp rationale).
extern "C" {

std::int32_t mihini_dtls_write_cb(dtls_context_t* ctx, session_t* session,
                                  uint8* data, size_t len) {
    auto* impl = static_cast<mihini::lwm2m::DtlsImpl*>(dtls_get_app_data(ctx));
    return static_cast<std::int32_t>(
        ::sendto(impl->fd, data, len, MSG_DONTWAIT, &session->addr.sa, session->size));
}

std::int32_t mihini_dtls_read_cb(dtls_context_t* ctx, session_t* session,
                                 uint8* data, size_t len) {
    auto* impl = static_cast<mihini::lwm2m::DtlsImpl*>(dtls_get_app_data(ctx));
    impl->session = *session;
    impl->responses.clear();
    if (impl->handler) {
        const std::string plaintext(reinterpret_cast<const char*>(data), len);
        impl->responses = impl->handler(plaintext, mihini::lwm2m::session_key(*session));
    }
    return static_cast<std::int32_t>(impl->responses.size());
}

std::int32_t mihini_dtls_event_cb(dtls_context_t* ctx, session_t* session,
                                  dtls_alert_level_t level, unsigned short code) {
    auto* impl = static_cast<mihini::lwm2m::DtlsImpl*>(dtls_get_app_data(ctx));
    if (!level && code == DTLS_EVENT_CONNECTED) {
        impl->connected = true;
    } else if (!level && code == DTLS_EVENT_CONNECT) {
        impl->connected = false;
    }
    (void)session;
    return 0;
}

std::int32_t mihini_dtls_psk_cb(dtls_context_t* ctx, const session_t* session,
                                dtls_credentials_type_t type,
                                const unsigned char* desc, size_t desc_len,
                                unsigned char* result, size_t result_length) {
    (void)session;
    auto* impl = static_cast<mihini::lwm2m::DtlsImpl*>(dtls_get_app_data(ctx));

    switch (type) {
        case DTLS_PSK_HINT:
            return 0; // no identity hint to advertise
        case DTLS_PSK_IDENTITY: {
            const std::string iden = impl->presented_identity();
            if (iden.empty() || iden.size() > result_length) {
                return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
            }
            std::memcpy(result, iden.data(), iden.size());
            return static_cast<std::int32_t>(iden.size());
        }
        case DTLS_PSK_KEY: {
            const std::string identity(reinterpret_cast<const char*>(desc), desc_len);
            const std::string secret = impl->resolve_secret(identity);
            if (secret.empty()) {
                return dtls_alert_fatal_create(DTLS_ALERT_ILLEGAL_PARAMETER);
            }
            const std::string bin = mihini::lwm2m::hex_to_binary(secret);
            if (bin.empty() || bin.size() > result_length) {
                return dtls_alert_fatal_create(DTLS_ALERT_INTERNAL_ERROR);
            }
            std::memcpy(result, bin.data(), bin.size());
            return static_cast<std::int32_t>(bin.size());
        }
        default:
            return -1;
    }
}

} // extern "C"

namespace mihini::lwm2m {

DtlsAdapter::DtlsAdapter() : impl_(new DtlsImpl) {
    impl_->cb.write = mihini_dtls_write_cb;
    impl_->cb.read = mihini_dtls_read_cb;
    impl_->cb.event = mihini_dtls_event_cb;
    impl_->cb.get_psk_info = mihini_dtls_psk_cb;
    impl_->cb.get_ecdsa_key = nullptr;
    impl_->cb.verify_ecdsa_key = nullptr;

    dtls_init();
    impl_->ctx = dtls_new_context(impl_.get());
    dtls_set_handler(impl_->ctx, &impl_->cb);
}

DtlsAdapter::~DtlsAdapter() = default;

DtlsAdapter::DtlsAdapter(DtlsAdapter&&) noexcept = default;
DtlsAdapter& DtlsAdapter::operator=(DtlsAdapter&&) noexcept = default;

bool DtlsAdapter::open(std::uint16_t port) {
    close();
    impl_->fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (impl_->fd < 0) return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(impl_->fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
        return false;
    }
    return true;
}

void DtlsAdapter::close() {
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
}

int DtlsAdapter::fd() const {
    return impl_->fd;
}

bool DtlsAdapter::connect(const std::string& host, std::uint16_t port) {
    if (!impl_->ctx || impl_->fd < 0) return false;
    set_session(impl_->session, host, port);
    impl_->peer = impl_->session;
    impl_->is_client = true;
    impl_->connected = false;

    // A prior stalled handshake leaves the peer non-connected; tinydtls then
    // only renegotiates instead of sending a fresh ClientHello. Reset it.
    dtls_peer_t* existing = dtls_get_peer(impl_->ctx, &impl_->session);
    if (existing && dtls_peer_state(existing) != DTLS_STATE_CONNECTED) {
        dtls_reset_peer(impl_->ctx, existing);
    }
    return dtls_connect(impl_->ctx, &impl_->session) >= 0;
}

bool DtlsAdapter::connected() const {
    return impl_->connected;
}

void DtlsAdapter::set_handler(InboundHandler h) {
    impl_->handler = std::move(h);
}

void DtlsAdapter::set_psk_resolver(PskResolver r) {
    impl_->resolver = std::move(r);
}

void DtlsAdapter::add_credential(const std::string& identity, const std::string& hex_secret) {
    if (impl_->bootstrap_identity.empty()) impl_->bootstrap_identity = identity;
    impl_->credentials[identity] = hex_secret; // upsert
}

void DtlsAdapter::active_identity(const std::string& id) {
    impl_->active_identity = id;
}

std::string DtlsAdapter::active_identity() const {
    return impl_->presented_identity();
}

int DtlsAdapter::rx() {
    if (impl_->fd < 0) return -1;

    std::vector<std::uint8_t> buf(DTLS_MAX_BUF);
    session_t session;
    std::memset(&session, 0, sizeof(session));
    session.size = sizeof(session.addr);

    const ssize_t len = ::recvfrom(impl_->fd, buf.data(), buf.size(), 0,
                                   &session.addr.sa, &session.size);
    if (len < 0) return -1;
    if (len > DTLS_MAX_BUF) return -1;

    const int ret = dtls_handle_message(impl_->ctx, &session, buf.data(),
                                        static_cast<int>(len));
    if (ret == 0) {
        // Reply to the peer this datagram came from, not impl_->session (which
        // the read callback may have switched mid-handling).
        for (const auto& rsp : impl_->responses) {
            dtls_write(impl_->ctx, &session,
                       const_cast<uint8*>(reinterpret_cast<const uint8*>(rsp.data())),
                       rsp.size());
        }
    }
    return ret;
}

int DtlsAdapter::tx(const std::string& wire) {
    return dtls_write(impl_->ctx, &impl_->session,
                      const_cast<uint8*>(reinterpret_cast<const uint8*>(wire.data())),
                      wire.size());
}

int DtlsAdapter::tx_peer(const std::string& wire) {
    return dtls_write(impl_->ctx, &impl_->peer,
                      const_cast<uint8*>(reinterpret_cast<const uint8*>(wire.data())),
                      wire.size());
}

bool DtlsAdapter::check_retransmit() {
    if (!impl_->ctx) return false;
    clock_time_t next = 0;
    bool is_max = false;
    dtls_check_retransmit(impl_->ctx, &next, &is_max);
    return is_max;
}

} // namespace mihini::lwm2m
