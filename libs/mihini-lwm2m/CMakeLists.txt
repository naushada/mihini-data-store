cmake_minimum_required(VERSION 3.16)

# Self-contained C++17 LwM2M 1.1 library: CoAP (RFC 7252), TLV (Core Annex C),
# SenML JSON (RFC 8428), the object/resource model, the Read/Write/Execute +
# Observe-Notify/Send ("push") dispatcher, and a DTLS 1.2 (PSK) transport over
# the vendored Eclipse tinydtls (see 3rdparty/). Reference shape: the iot repo's
# coap_adapter + lwm2m_dm_client + dtls_adapter.
#
#   add_subdirectory(libs/mihini-lwm2m)
#   target_link_libraries(your_app PRIVATE mihini-lwm2m)

add_subdirectory(3rdparty/tinydtls)

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
