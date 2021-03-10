find_package(TBB CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

file(GLOB_RECURSE PUBLIC_HEADERS_FABRIC include/*)

add_custom_target(include_fabricBased ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        ${CMAKE_CURRENT_BINARY_DIR}/include/networklayer/
        DEPENDS ${PUBLIC_HEADERS_FABRIC}
        )

add_library(fabricBased INTERFACE)
target_link_libraries(fabricBased INTERFACE rpc fabric pthread TBB::tbb)
target_include_directories(fabricBased INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(fabricBased INTERFACE spdlog::spdlog spdlog::spdlog_header_only)
add_dependencies(fabricBased include_fabricBased)

add_executable(rpc_client example/rpc_client.cc)
target_link_libraries(rpc_client fabricBased)

add_executable(rpc_server example/rpc_server.cc)
target_link_libraries(rpc_server fabricBased)

add_executable(connectionless_client example/connectionless_client.cc)
target_link_libraries(connectionless_client fabricBased)

add_executable(connectionless_server example/connectionless_server.cc)
target_link_libraries(connectionless_server fabricBased)

add_executable(connectionlessTest test/connectionlessTest.cc)
target_link_libraries(connectionlessTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main fabricBased)
add_test(connectionlessTest connectionlessTest)

add_executable(fabricTest test/fabricTest.cc)
target_link_libraries(fabricTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main fabricBased)
add_test(fabricTest fabricTest)