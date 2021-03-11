FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/usr/local/lib

RUN apt update && apt install -y cmake build-essential git curl zip unzip tar pkg-config wget bzip2

RUN wget https://github.com/ofiwg/libfabric/releases/download/v1.9.1/libfabric-1.9.1.tar.bz2 && \
    bunzip2 libfabric-1.9.1.tar.bz2 && tar xf libfabric-1.9.1.tar && cd libfabric-1.9.1 && ./configure && \
    make -j && make install

COPY . /network-layer

WORKDIR /network-layer

RUN ./vcpkg/bootstrap-vcpkg.sh && ./vcpkg/vcpkg install tbb gtest

RUN mkdir build && cd build && cmake -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake .. && \
    make -j && ctest
