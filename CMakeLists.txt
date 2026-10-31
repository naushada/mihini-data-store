cmake_minimum_required(VERSION 3.16)

project(mihini-data-store LANGUAGES CXX)

# Build everything:
#
#   cmake -B build
#   cmake --build build
#   ./build/apps/datastore-cli/datastore-cli
#
# See libs/mihini-datastore/CMakeLists.txt for enabling the EMP backend.

add_subdirectory(libs/mihini-datastore)
add_subdirectory(apps/datastore-cli)
