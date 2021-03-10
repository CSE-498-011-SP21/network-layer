configure_file(include/networklayer/config.hh.in include/networklayer/config.hh)

add_library(rpc INTERFACE)
target_include_directories(rpc INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>" "$<INSTALL_INTERFACE:include>")
target_include_directories(rpc INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>")

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include DESTINATION include)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include DESTINATION include)

add_executable(fabricsGet src/fabricsGet.cc)
target_link_libraries(fabricsGet PRIVATE fabric)
target_link_libraries(fabricsGet PRIVATE rpc)