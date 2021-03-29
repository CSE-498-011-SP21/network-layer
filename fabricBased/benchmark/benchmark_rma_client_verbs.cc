#define COMPILE_LOG ERROR

#include <networklayer/connection.hh>
#include <chrono>

int LOG_LEVEL = TRACE;

int main(int argc, char **argv) {

    std::string addr = "127.0.0.1";

    if (argc > 1) {
        addr = argv[1];
    }
    int port = 8080;
    auto *c2 = new cse498::Connection(addr.c_str(), false, port, cse498::Verbs);
    c2->connect();

    cse498::unique_buf buf;

    uint64_t key = 1;
    c2->register_mr(buf, FI_SEND | FI_RECV | FI_READ | FI_WRITE | FI_REMOTE_READ, key);

    std::cerr << "Recv" << std::endl;

    std::cerr << "Using buffer " << (void *) buf.get() << std::endl;
    c2->recv(buf, 4096);

    uint64_t remoteKey = *((uint64_t *) buf.get());

    std::cerr << "Remote key is " << remoteKey << std::endl;

    c2->recv(buf, 4096);

    uint64_t remoteAddr = *((uint64_t *) buf.get());

    std::cerr << "Remote addr is " << (void *) remoteAddr << std::endl;

    *((uint64_t *) buf.get()) = 10;

    c2->read(buf, sizeof(uint64_t), remoteAddr, remoteKey);

    while ((*(uint64_t *) buf.get()) != ~0);

    std::cerr << "Read: " << *(uint64_t *) buf.get() << std::endl;

    std::cerr << "Send" << std::endl;

    c2->send(buf, 1);

    size_t size = 8;

    while (size <= 4096) {
        for (int rep = 0; rep < 5; rep++) {
            usleep(1000); // sleep for a ms
            auto start = std::chrono::high_resolution_clock::now();
            c2->send(buf, size);
            auto end = std::chrono::high_resolution_clock::now();
            std::cout << "two-sided" << "\t" << size << "\t" << std::chrono::duration<double>(end - start).count() * 1e9
                      << std::endl;
        }
        size = size << 1;
    }

    size = 8;

    while (size <= 4096) {
        for (int rep = 0; rep < 5; rep++) {
            auto start = std::chrono::high_resolution_clock::now();
            c2->read(buf, size, remoteAddr, remoteKey);
            auto end = std::chrono::high_resolution_clock::now();
            std::cout << "read" << "\t" << size << "\t" << std::chrono::duration<double>(end - start).count() * 1e9
                      << std::endl;
        }
        size = size << 1;
    }

    c2->send(buf, 1);

    size = 8;

    while (size <= 4096) {
        for (int rep = 0; rep < 5; rep++) {
            auto start = std::chrono::high_resolution_clock::now();
            c2->write(buf, size, remoteAddr, remoteKey);
            auto end = std::chrono::high_resolution_clock::now();
            std::cout << "write" << "\t" << size << "\t" << std::chrono::duration<double>(end - start).count() * 1e9
                      << std::endl;
        }
        size = size << 1;
    }

    c2->send(buf, 1);

    return 0;
}
