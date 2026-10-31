cmake_minimum_required(VERSION 3.16)

# Declare a project so this tree also builds standalone (as its own Yocto
# recipe); as a subdirectory it merely scopes PROJECT_* here and re-declares
# CXX (already enabled by the parent).
project(lwm2m CXX)

# Self-contained C++17 LwM2M 1.1 library: CoAP (RFC 7252), TLV (Core Annex C),
# SenML JSON (RFC 8428), the object/resource model, the Read/Write/Execute +
# Observe-Notify/Send ("push") dispatcher, and a DTLS 1.2 (PSK) transport over
# the vendored Eclipse tinydtls (see 3rdparty/). The transport I/O loop (socket
# readiness + the handshake-retransmit timer) is driven by libevent. Reference
# shape: the iot repo's coap_adapter + lwm2m_dm_client + dtls_adapter.
#
#   add_subdirectory(libs/lwm2m)
#   target_link_libraries(your_app PRIVATE lwm2m)

# tinydtls is vendored under 3rdparty/ and built as a subdirectory by default
# (the self-contained path). On-target Yocto builds link the system tinydtls
# from the tinydtls recipe instead, so libtinydtls.a is owned once, by that
# recipe, and the archive is not duplicated across packages.
option(LWM2M_SYSTEM_TINYDTLS
       "Link a system-installed tinydtls instead of the vendored subdirectory" OFF)
if(LWM2M_SYSTEM_TINYDTLS)
    find_path(TINYDTLS_INCLUDE_DIR NAMES tinydtls.h PATH_SUFFIXES tinydtls REQUIRED)
    find_library(TINYDTLS_LIBRARY NAMES tinydtls REQUIRED)
    add_library(tinydtls STATIC IMPORTED)
    set_target_properties(tinydtls PROPERTIES
        IMPORTED_LOCATION "${TINYDTLS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TINYDTLS_INCLUDE_DIR}")
else()
    add_subdirectory(3rdparty/tinydtls)
endif()

# libevent drives the transport I/O loop (UDP socket readiness + the DTLS
# handshake-retransmit timer). On-target it comes from the libevent recipe; on
# macOS Homebrew it lives under /opt/homebrew (or /usr/local), outside the
# default search path.
find_path(LIBEVENT_INCLUDE_DIR NAMES event2/event.h
          HINTS /opt/homebrew /usr/local
          PATH_SUFFIXES include)
find_library(LIBEVENT_LIBRARY NAMES event
            HINTS /opt/homebrew /usr/local
            PATH_SUFFIXES lib)
if(NOT LIBEVENT_INCLUDE_DIR OR NOT LIBEVENT_LIBRARY)
    message(FATAL_ERROR "libevent not found (required by the DTLS transport)")
endif()

add_library(lwm2m STATIC
    src/coap.cpp
    src/tlv.cpp
    src/senml.cpp
    src/model.cpp
    src/engine.cpp
    src/dtls.cpp
)

target_include_directories(lwm2m
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${LIBEVENT_INCLUDE_DIR}
)

target_compile_features(lwm2m PUBLIC cxx_std_17)

# tinydtls + libevent are only needed by the DTLS transport; keep them PRIVATE
# so the public LwM2M headers stay dependency-free for codec-only consumers.
target_link_libraries(lwm2m PRIVATE tinydtls ${LIBEVENT_LIBRARY})

# Unit tests. Enabled through the top-level project's CTest, or run directly.
# GoogleTest is fetched at configure time (pinned release) so the tests build
# identically on a host and inside a container, with no system gtest package.
include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    set(INSTALL_GTEST OFF)   # subproject: don't install gtest artifacts
    set(BUILD_GMOCK OFF)     # only gtest is needed here
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)

    add_executable(test_lwm2m test/test_lwm2m.cpp)
    target_link_libraries(test_lwm2m PRIVATE lwm2m gtest_main)
    gtest_discover_tests(test_lwm2m)

    add_executable(test_dtls test/test_dtls.cpp)
    target_link_libraries(test_dtls PRIVATE lwm2m gtest_main)
    target_include_directories(test_dtls PRIVATE ${LIBEVENT_INCLUDE_DIR})
    gtest_discover_tests(test_dtls)
endif()
