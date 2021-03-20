//
// Created by depaulsmiller on 3/17/21.
//

#include <networklayer/connection.hh>
#include <unistd.h>

int LOG_LEVEL = DEBUG;

int main() {

    volatile char *remoteAccess = new char[sizeof(uint64_t)];
    int port = 8080;
    const char* addr = "127.0.0.1";

    cse498::Connection *c1 = new cse498::Connection(addr, port, []() {});

    *((uint64_t *) remoteAccess) = ~0;
    c1->register_mr((char *) remoteAccess, sizeof(uint64_t), FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ,
                    1);

    char* buf = new char[1];

    std::cerr << "Send\n";

    c1->wait_send(buf, 1);

    std::cerr << "Recv\n";

    c1->wait_recv(buf, 1);

    return 0;
}