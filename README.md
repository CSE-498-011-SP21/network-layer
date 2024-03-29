# Network Layer

Network layer code.

## Requirements

- gtest
- libfabric 1.9.1
- tbb
- doxygen optional to build documentation

Run the following to ensure the network layer is working:

```bash
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

## Building

To build in docker generate a public/private keypair using ssh-keygen and specify the
output file as docker\_rsa. Register this pair with github.
Then use docker build to build the container.

## Important Notes

- Broadcast primitives on connectionless clients and servers can be used in conjunction with
send, recv, etc.
- Sockets provider type appears to work with an offset as the remote address and ignores the 
desire to use a virtual address, while verbs does use a virtual address. These have to be exchanged
between servers.