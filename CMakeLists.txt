cmake_minimum_required(VERSION 3.0)

project(NetworkLayer VERSION 0.0.1)

find_package(GTest CONFIG REQUIRED)

enable_testing()

add_subdirectory(common)

#add_subdirectory(socketBased)

add_subdirectory(fabricBased)

#add_subdirectory(eRPC)