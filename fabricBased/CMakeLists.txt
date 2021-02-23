project(fabricBased)

find_package(TBB CONFIG REQUIRED)

add_executable(client src/client.cc)
target_include_directories(client PRIVATE include)
target_link_libraries(client rpc fabric pthread TBB::tbb)

add_executable(server src/server.cc)
target_include_directories(server PRIVATE include)
target_link_libraries(server rpc fabric pthread TBB::tbb)