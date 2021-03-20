# Network Layer
Network layer code.

## Requirements

- gtest
- libfabric 1.9.1
- tbb
- doxygen optional to build documentation

Run the following to ensure the network layer is working:
```
git submodule init
git submodule update
./vcpkg/bootstrap-vcpkg.sh

# Install libfabric 1.9.1

./vcpkg/vcpkg install gtest tbb

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

Broadcast primitives on connectionless clients and servers can be used in conjunction with
send, recv, etc.