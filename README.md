# Network Layer
Network layer code.

## Requirements

- gtest
- libfabric 1.6
- tbb
- spdlog
- doxygen optional to build documentation

Run the following to ensure the network layer is working:
```
git submodule init
git submodule update
./vcpkg/bootstrap-vcpkg.sh

# Use brew, apt, or yum to install libfabric

./vcpkg/vcpkg install gtest tbb spdlog

mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake ..
make -j
ctest
```

Make sure to write a static assert in your program that you are using the correct version
by including networklayer/config.hh.

Example:
```c++
#include <networklayer/config.hh>
static_assert(NETWORK_VER_MAJOR == 0 
            && NETWORK_VER_MINOR == 0 
            && NETWORK_VER_PATCH == 1, 
            "Need to ensure using the correct version");
```

## Notes

Broadcast primitives on connectionless clients and servers should be used alone
(i.e. without send and receive ever used on client and server)