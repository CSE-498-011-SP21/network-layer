cmake_minimum_required(VERSION 3.0)

project(NetworkLayer VERSION 0.2.0)

find_package(GTest CONFIG REQUIRED)

find_package(Doxygen)

enable_testing()

add_subdirectory(commonAPI)

add_subdirectory(common)

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
include(CPack)
