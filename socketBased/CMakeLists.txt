project(socketBased)

find_package(Boost)

if(${BOOST_FOUND})

    find_package(TBB CONFIG REQUIRED)

    message(STATUS "Able to build socket based protocol")
    add_library(socketBased INTERFACE)
    target_include_directories(socketBased INTERFACE include)
    target_link_libraries(socketBased INTERFACE rpc TBB::tbb pthread)

    add_executable(socketBasedTest test/socketBasedTest.cc)
    target_link_libraries(socketBasedTest socketBased)

else()
    message(STATUS "Not able to build socket based protocol")
endif()