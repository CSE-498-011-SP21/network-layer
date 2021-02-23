FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y libfabric-dev cmake build-essential git

COPY . /network-layer

WORKDIR /network-layer

RUN ./vcpkg/bootstrap-vcpkg.sh && ./vcpkg/vcpkg install tbb gtest

RUN mkdir build && cd build && cmake -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake .. && \
    make -j && ctest
