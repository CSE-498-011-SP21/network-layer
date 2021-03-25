cmake_minimum_required(VERSION 3.14)

project(NetworkLayer VERSION 2.1.0)

include(FetchContent)

FetchContent_Declare(
        common
        GIT_REPOSITORY git@github.com:CSE-498-011-SP21/common.git
        GIT_TAG main
)

FetchContent_MakeAvailable(common)

add_library(Common::kvcg_stuff ALIAS kvcg_stuff)

set(CMAKE_CXX_STANDARD 17)

find_package(GTest CONFIG REQUIRED)

find_package(Doxygen)

enable_testing()

add_subdirectory(commonAPI)

add_subdirectory(fabricBased)

add_subdirectory(docs)

install(TARGETS rpc fabricBased
        EXPORT NetworklayerTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
        )

install(EXPORT NetworklayerTargets
        FILE NetworklayerTargets.cmake
        NAMESPACE Networklayer::
        DESTINATION lib/cmake/Networklayer
        )

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/NetworkLayerConfig.cmake"
        INSTALL_DESTINATION lib/cmake/NetworkLayer
        )

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NetworkLayerConfig.cmake"
#        "${CMAKE_CURRENT_BINARY_DIR}/NetworkLayerConfigVersion.cmake"
        DESTINATION lib/cmake/NetworkLayer
        )

set(CPACK_PACKAGE_VENDOR "dePaul, Jimmy, and Michael")
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "dePaul, Jimmy, and Michael")
set(CPACK_SOURCE_GENERATOR "DEB")
set(CPACK_SOURCE_IGNORE_FILES
        /.git
        /dist
        /.*build.*
        /\\\\.DS_Store
        )
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libfabric")

include(CPack)
