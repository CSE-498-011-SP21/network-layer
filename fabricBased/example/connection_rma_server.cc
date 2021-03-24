//
// Created by depaulsmiller on 3/17/21.
//

#include <networklayer/connection.hh>
#include <unistd.h>

int LOG_LEVEL = DEBUG;

int main(int argc, char **argv) {

    cse498::unique_buf remoteAccess, buf;

    const char *addr = "127.0.0.1";

    if (argc > 1) {
        addr = argv[1];
    }

    auto *c1 = new cse498::Connection(addr, true);

    *((uint64_t *) remoteAccess.get()) = ~0;
    uint64_t key = 1;
    c1->register_mr(remoteAccess, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, key);

    std::cerr << "Send\n";

    c1->send(buf, 1);

    std::cerr << "Recv\n";

    c1->recv(buf, 1);

    return 0;
}
