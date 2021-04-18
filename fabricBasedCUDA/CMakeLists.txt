find_package(TBB CONFIG REQUIRED)

add_library(fabricBasedCUDA INTERFACE)
target_link_libraries(fabricBasedCUDA INTERFACE rpc pthread TBB::tbb)
target_include_directories(fabricBasedCUDA INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>")
target_link_libraries(fabricBasedCUDA INTERFACE Common::kvcg_stuff)
target_link_libraries(fabricBasedCUDA INTERFACE fabric)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)

add_executable(connection_rma_client_cuda example/connection_rma_client.cu)
target_link_libraries(connection_rma_client_cuda fabricBasedCUDA)

add_executable(connection_rma_server_cuda example/connection_rma_server.cu)
target_link_libraries(connection_rma_server_cuda fabricBasedCUDA)