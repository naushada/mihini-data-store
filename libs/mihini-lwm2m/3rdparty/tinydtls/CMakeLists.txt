cmake_minimum_required(VERSION 3.16)

# Vendored Eclipse tinydtls 0.8.6 — a minimal DTLS 1.2 implementation for
# constrained devices, used here as the CoAPs transport under LwM2M (DTLS-PSK,
# TLS_PSK_WITH_AES_128_CCM_8). Copied from the iot repo's apps/3rdparty/tinydtls
# (legatoproject fork) with its local patches already applied:
#   * /dev/urandom PRNG (prng.h) instead of rand()
#   * session-id bounds check in check_server_hello (dtls.c)
#   * uthash expand_mult overflow guard (uthash.h)
#   * dtls_set_log_sink() host hook (dtls_debug.{h,c})
# See LICENSE for the EPL-1.0 | EDL-1.0 dual license.

# The parent project() enables only CXX; tinydtls is C, so opt the language in.
enable_language(C)

add_library(tinydtls STATIC
    dtls.c
    crypto.c
    ccm.c
    hmac.c
    netq.c
    peer.c
    dtls_time.c
    session.c
    dtls_debug.c
    aes/rijndael.c
    ecc/ecc.c
    sha2/sha2.c
)

target_include_directories(tinydtls
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/aes
        ${CMAKE_CURRENT_SOURCE_DIR}/ecc
        ${CMAKE_CURRENT_SOURCE_DIR}/sha2
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

# Match the iot repo's Makefile flags. DTLS_PSK / DTLS_ECC are already on via
# the generated dtls_config.h; these two make the DTLS 1.2 + SHA256 PRF paths
# unconditional (global.h would otherwise define them, but be explicit).
target_compile_definitions(tinydtls PRIVATE DTLSv12 WITH_SHA256)

set_target_properties(tinydtls PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
