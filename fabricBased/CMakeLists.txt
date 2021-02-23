find_package(TBB CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(fabricBased INTERFACE)
target_link_libraries(fabricBased INTERFACE rpc fabric pthread TBB::tbb)
target_include_directories(fabricBased INTERFACE include)
target_link_libraries(fabricBased INTERFACE spdlog::spdlog spdlog::spdlog_header_only)

add_executable(client example/client.cc)
target_link_libraries(client fabricBased)

add_executable(server example/server.cc)
target_link_libraries(server fabricBased)

add_executable(fabricTest test/fabricTest.cc)
target_link_libraries(fabricTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main fabricBased)
add_test(fabricTest fabricTest)