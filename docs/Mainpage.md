# Network Layer

This is the network layer code.

Make sure to write a static assert in your program that you are using the correct version
by including networklayer/config.hh.

Example for Version 0.0.1:
```
#include <networklayer/config.hh>
static_assert(NETWORK_VER_MAJOR == 0 && NETWORK_VER_MINOR == 0 && NETWORK_VER_PATCH == 1, "Need to ensure using the correct version");
```
