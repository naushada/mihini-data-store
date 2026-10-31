cmake_minimum_required(VERSION 3.16)

# Declare a project so this tree also builds standalone (as its own Yocto
# recipe); as a subdirectory it merely scopes PROJECT_* here and re-declares
# CXX (already enabled by the parent).
project(mihini-lwm2m CXX)

# Self-contained C++17 LwM2M 1.1 library: CoAP (RFC 7252), TLV (Core Annex C),
# SenML JSON (RFC 8428), the object/resource model, the Read/Write/Execute +
# Observe-Notify/Send ("push") dispatcher, and a DTLS 1.2 (PSK) transport over
# the vendored Eclipse tinydtls (see 3rdparty/). Reference shape: the iot repo's
# coap_adapter + lwm2m_dm_client + dtls_adapter.
#
#   add_subdirectory(libs/mihini-lwm2m)
#   target_link_libraries(your_app PRIVATE mihini-lwm2m)

# tinydtls is vendored under 3rdparty/ and built as a subdirectory by default
# (the self-contained path). On-target Yocto builds link the system tinydtls
# from the tinydtls recipe instead, so libtinydtls.a is owned once, by that
# recipe, and the archive is not duplicated across packages.
option(MIHINI_LWM2M_SYSTEM_TINYDTLS
       "Link a system-installed tinydtls instead of the vendored subdirectory" OFF)
if(MIHINI_LWM2M_SYSTEM_TINYDTLS)
    find_path(TINYDTLS_INCLUDE_DIR NAMES tinydtls.h PATH_SUFFIXES tinydtls REQUIRED)
    find_library(TINYDTLS_LIBRARY NAMES tinydtls REQUIRED)
    add_library(tinydtls STATIC IMPORTED)
    set_target_properties(tinydtls PROPERTIES
        IMPORTED_LOCATION "${TINYDTLS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TINYDTLS_INCLUDE_DIR}")
else()
    add_subdirectory(3rdparty/tinydtls)
endif()

add_library(mihini-lwm2m STATIC
    src/coap.cpp
    src/tlv.cpp
    src/senml.cpp
    src/model.cpp
    src/engine.cpp
    src/dtls.cpp
)

target_include_directories(mihini-lwm2m
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(mihini-lwm2m PUBLIC cxx_std_17)

# tinydtls is only needed by the DTLS transport; keep it a PRIVATE link so the
# public LwM2M headers stay free of the C dependency for codec-only consumers.
target_link_libraries(mihini-lwm2m PRIVATE tinydtls)

# Unit tests. Enabled through the top-level project's CTest, or run directly.
include(CTest)
if(BUILD_TESTING)
    add_executable(test_lwm2m test/test_lwm2m.cpp)
    target_link_libraries(test_lwm2m PRIVATE mihini-lwm2m)
    add_test(NAME lwm2m COMMAND test_lwm2m)

    add_executable(test_dtls test/test_dtls.cpp)
    target_link_libraries(test_dtls PRIVATE mihini-lwm2m)
    add_test(NAME dtls COMMAND test_dtls)
endif()
