add_executable(datastore-cli main.cpp)

target_link_libraries(datastore-cli PRIVATE mihini-datastore)

target_compile_features(datastore-cli PRIVATE cxx_std_17)
