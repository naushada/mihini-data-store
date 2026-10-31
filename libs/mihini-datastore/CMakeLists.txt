cmake_minimum_required(VERSION 3.16)

# Static C++ library bridging the Mihini device-tree data store and C++ apps.
#
#   add_subdirectory(libs/mihini-datastore)
#   target_link_libraries(your_app PRIVATE mihini-datastore)
#
# By default the library builds against the offline in-memory backend only.
# To also build the EMP backend against the vendored swi_* C client, configure with:
#
#   cmake -DMIHINI_DATSTORE_WITH_SWILIBS=ON \
#         -DMIHINI_SWILIBS_INCLUDE_DIRS="<...>/cfwk/devicetree;<...>/cfwk/racon;<...>/libs/common" \
#         -DMIHINI_SWILIBS_LIBRARIES="swi_devicetree;swi_dset;bysant;yajl" \
#         -DMIHINI_SWILIBS_LIBRARY_DIRS="<...>" \
#         ..
#
# All of the option / MIHINI_SWILIBS_* variables are optional unless the EMP
# backend is enabled.

add_library(mihini-datastore STATIC
    src/datastore.cpp
    src/inmemory_backend.cpp
    src/emp_backend.cpp
)

target_include_directories(mihini-datastore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(mihini-datastore PUBLIC cxx_std_17)

option(MIHINI_DATSTORE_WITH_SWILIBS
       "Build the EMP backend against the swi_devicetree/swi_dset C client" OFF)

if(MIHINI_DATSTORE_WITH_SWILIBS)
    target_compile_definitions(mihini-datastore PRIVATE MIHINI_DATSTORE_WITH_SWILIBS)

    set(MIHINI_SWILIBS_INCLUDE_DIRS "" CACHE STRING
        "Semicolon-separated include dirs for swi_devicetree/swi_dset/swi_status headers")
    set(MIHINI_SWILIBS_LIBRARY_DIRS "" CACHE STRING
        "Semicolon-separated library dirs for the swi_* static libraries")
    set(MIHINI_SWILIBS_LIBRARIES "swi_devicetree;swi_dset;bysant;yajl" CACHE STRING
        "Semicolon-separated names of the swi_* client libraries to link")

    target_include_directories(mihini-datastore PRIVATE ${MIHINI_SWILIBS_INCLUDE_DIRS})
    target_link_directories(mihini-datastore PRIVATE ${MIHINI_SWILIBS_LIBRARY_DIRS})
    target_link_libraries(mihini-datastore PRIVATE ${MIHINI_SWILIBS_LIBRARIES})
endif()
