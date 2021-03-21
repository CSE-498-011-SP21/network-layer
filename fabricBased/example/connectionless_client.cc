//
// Created by depaulsmiller on 3/9/21.
//

#include <networklayer/connectionless.hh>
#include <iostream>

int LOG_LEVEL = TRACE;

int main(int argc, char **argv) {

    std::string addr = "127.0.0.1";
    if (argc > 1) {
        addr = std::string(argv[1]);
    }

    //spdlog::debug("Using addr {}", addr);

    cse498::ConnectionlessClient c(addr.c_str(), 8080);
    const char *s = "hi";

    char *buf = new char[4096];
    cse498::mr_t mr;

    c.registerMR(buf, 4096, mr);

    // we can use registered memory for send
    c.connect(buf, 4096);

    char *local = new char[3];
    memcpy(local, s, 3);

    // we can use unregistered memory for send
    c.send(local, 3);

    // use registered memory for recv
    c.recv(buf, 3);

    std::cout << buf << std::endl;

    // always free mr
    cse498::free_mr(mr);

    delete[] local;
    delete[] buf;

    return 0;
}