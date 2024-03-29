find_package(TBB CONFIG REQUIRED)

add_library(fabricBased INTERFACE)
target_link_libraries(fabricBased INTERFACE rpc pthread TBB::tbb)
target_include_directories(fabricBased INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>")
target_link_libraries(fabricBased INTERFACE Common::kvcg_stuff)
target_link_libraries(fabricBased INTERFACE fabric)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)

add_executable(rpc_client example/rpc_client.cc)
target_link_libraries(rpc_client fabricBased)

add_executable(rpc_server example/rpc_server.cc)
target_link_libraries(rpc_server fabricBased)

add_executable(connectionless_client example/connectionless_client.cc)
target_link_libraries(connectionless_client fabricBased)

add_executable(connectionless_server example/connectionless_server.cc)
target_link_libraries(connectionless_server fabricBased)

add_library(broadcastLibrary test/broadcastLibrary.cc)
target_link_libraries(broadcastLibrary PUBLIC fabricBased)

add_library(fabricLibrary test/fabricLibrary.cc)
target_link_libraries(fabricLibrary PUBLIC fabricBased)

add_executable(connectionlessTest test/connectionlessTest.cc)
target_link_libraries(connectionlessTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main broadcastLibrary fabricBased)
add_test(connectionlessTest connectionlessTest)

add_executable(connection example/connection.cc)
target_link_libraries(connection fabricBased)

add_executable(connection_rma_client example/connection_rma_client.cc)
target_link_libraries(connection_rma_client fabricBased)

add_executable(connection_rma_server example/connection_rma_server.cc)
target_link_libraries(connection_rma_server fabricBased)

add_executable(connection_rma_client_verbs example/connection_rma_client_verbs.cc)
target_link_libraries(connection_rma_client_verbs fabricBased)

add_executable(connection_rma_server_verbs example/connection_rma_server_verbs.cc)
target_link_libraries(connection_rma_server_verbs fabricBased)

add_executable(benchmark_rma_client benchmark/benchmark_rma_client.cc)
target_link_libraries(benchmark_rma_client fabricBased)

add_executable(benchmark_rma_client_verbs benchmark/benchmark_rma_client_verbs.cc)
target_link_libraries(benchmark_rma_client_verbs fabricBased)

add_executable(benchmark_rma_server benchmark/benchmark_rma_server.cc)
target_link_libraries(benchmark_rma_server fabricBased)

add_executable(benchmark_rma_server_verbs benchmark/benchmark_rma_server_verbs.cc)
target_link_libraries(benchmark_rma_server_verbs fabricBased)

add_executable(connectionTest test/connectionTest.cc)
target_link_libraries(connectionTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main broadcastLibrary fabricBased)
add_test(connectionTest connectionTest)

add_executable(fabricTest test/fabricTest.cc)
target_link_libraries(fabricTest PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main fabricLibrary fabricBased)
#add_test(fabricTest fabricTest)