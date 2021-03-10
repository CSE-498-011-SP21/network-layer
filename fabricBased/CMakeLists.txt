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

add_executable(client example/client.cc)
target_link_libraries(client fabricBased)

add_executable(server example/server.cc)
target_link_libraries(server fabricBased)

add_executable(connectionlessTest test/connectionlessTest.cc)
target_link_libraries(connectionlessTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main fabricBased)
add_test(connectionlessTest connectionlessTest)

add_executable(fabricTest test/fabricTest.cc)
target_link_libraries(fabricTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main fabricBased)
add_test(fabricTest fabricTest)