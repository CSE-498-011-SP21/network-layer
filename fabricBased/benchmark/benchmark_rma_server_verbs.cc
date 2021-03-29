//
// Created by depaulsmiller on 3/17/21.
//
#define COMPILE_LOG ERROR

#include <networklayer/connection.hh>
#include <unistd.h>

int LOG_LEVEL = TRACE;

int main(int argc, char **argv) {

    cse498::unique_buf remoteAccess, buf;

    const char *addr = "127.0.0.1";

    if (argc > 1) {
        addr = argv[1];
    }

    auto *c1 = new cse498::Connection(addr, true, 8080, cse498::Verbs);

    c1->connect();

    *((uint64_t *) remoteAccess.get()) = ~0;
    uint64_t key = 1;
    c1->register_mr(remoteAccess, FI_SEND | FI_RECV | FI_READ | FI_WRITE | FI_REMOTE_WRITE | FI_REMOTE_READ, key);
    uint64_t key2 = 2;
    c1->register_mr(buf, FI_SEND | FI_RECV | FI_READ | FI_WRITE, key2);

    std::cerr << "Send\n";

    *((uint64_t *) buf.get()) = key;

    c1->send(buf, sizeof(uint64_t));

    std::cerr << "Send\n";

    *((uint64_t *) buf.get()) = (uint64_t) remoteAccess.get();
    //*((uint64_t *) buf.get()) = (uint64_t) 0;

    c1->send(buf, sizeof(uint64_t));

    std::cerr << "Recv\n";

    c1->recv(buf, 1);

    size_t size = 8;

    while (size <= 4096) {
        for (int rep = 0; rep < 5; rep++) {
            c1->recv(buf, size);
        }
        size = size << 1;
    }

    c1->recv(buf, 1);

    c1->recv(buf, 1);

    return 0;
}
