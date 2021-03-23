//
// Created by depaulsmiller on 3/9/21.
//

#include <networklayer/connectionless.hh>
#include <thread>

int LOG_LEVEL = TRACE;

int main(int argc, char **argv) {
    //spdlog::set_level(spdlog::level::trace);

    std::string addr = "127.0.0.1";
    if (argc > 1) {
        addr = std::string(argv[1]);
    }

    //spdlog::debug("Using addr {}", addr);

    cse498::ConnectionlessServer c(addr.c_str(), 8080, FI_PROTO_RXM);

    char *buf = new char[4096];
    cse498::mr_t mr;

    c.registerMR(buf, 4096, mr);

    cse498::addr_t a;

    // we can use registered memory for send
    a = c.accept(buf, 4096);

    // use registered memory to recv
    c.recv(a, buf, 3);

    std::cout << buf << std::endl;

    // can use registered memory for send
    c.send(a, buf, 3);

    // always free mr
    cse498::free_mr(mr);

    delete[] buf;

    return 0;
}