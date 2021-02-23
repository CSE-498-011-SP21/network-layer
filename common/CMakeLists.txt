set(NAME_OF_DIR networklayer)
configure_file(include/config.hh.in include/${NAME_OF_DIR}/config.hh)

add_custom_target(include_common ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        ${CMAKE_CURRENT_BINARY_DIR}/include/${NAME_OF_DIR}/
        )

add_library(rpc INTERFACE)
target_include_directories(rpc INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include)
add_dependencies(rpc include_common)

add_executable(fabricsGet src/fabricsGet.cc)
target_link_libraries(fabricsGet PRIVATE fabric)
target_link_libraries(fabricsGet PRIVATE rpc)