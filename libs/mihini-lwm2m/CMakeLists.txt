cmake_minimum_required(VERSION 3.16)

# Self-contained C++17 LwM2M 1.1 library: CoAP (RFC 7252), TLV (Core Annex C),
# SenML JSON (RFC 8428), the object/resource model, and the Read/Write/Execute +
# Observe-Notify/Send ("push") dispatcher. No external dependencies; the optional
# DTLS transport is the caller's concern (see the iot repo's dtls_adapter for
# tinydtls-PSK). Reference shape: the iot repo's coap_adapter + lwm2m_dm_client.
#
#   add_subdirectory(libs/mihini-lwm2m)
#   target_link_libraries(your_app PRIVATE mihini-lwm2m)

add_library(mihini-lwm2m STATIC
    src/coap.cpp
    src/tlv.cpp
    src/senml.cpp
    src/model.cpp
    src/engine.cpp
)

target_include_directories(mihini-lwm2m
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(mihini-lwm2m PUBLIC cxx_std_17)

# Unit test. Enabled through the top-level project's CTest, or run it directly.
include(CTest)
if(BUILD_TESTING)
    add_executable(test_lwm2m test/test_lwm2m.cpp)
    target_link_libraries(test_lwm2m PRIVATE mihini-lwm2m)
    add_test(NAME lwm2m COMMAND test_lwm2m)
endif()
