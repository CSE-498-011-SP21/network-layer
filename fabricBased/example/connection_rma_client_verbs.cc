//
// Created by depaulsmiller on 3/17/21.
//

#include <networklayer/connection.hh>

int LOG_LEVEL = TRACE;

int main(int argc, char** argv){
    
    std::string addr = "127.0.0.1";

    if(argc > 1){
        addr = argv[1];
    }
    int port = 8080;
    auto *c2 = new cse498::Connection(addr.c_str(), false, port, cse498::Verbs);

    cse498::unique_buf buf;

    uint64_t key = 1;
    c2->register_mr(buf, FI_SEND | FI_RECV | FI_READ | FI_WRITE | FI_REMOTE_READ, key);

    std::cerr << "Recv" << std::endl;

    std::cerr << "Using buffer " << (void*) buf.get() << std::endl; 
    c2->recv(buf, 4096);

    uint64_t remoteKey = *((uint64_t *) buf.get());

    std::cerr << "Remote key is " << remoteKey << std::endl;

    c2->recv(buf, 4096);

    uint64_t remoteAddr = *((uint64_t *) buf.get());

    std::cerr << "Remote addr is " << (void*) remoteAddr << std::endl;

    *((uint64_t *) buf.get()) = 10;

    c2->read(buf, sizeof(uint64_t), remoteAddr, remoteKey);

    while ((*(uint64_t *) buf.get()) != ~0);

    std::cerr << "Read: " << *(uint64_t *) buf.get() << std::endl;

    std::cerr << "Send" << std::endl;

    c2->send(buf, 1);

    // connection_rma_server_verbs: prov/util/src/util_mem_monitor.c:132: ofi_monitor_cleanup: Assertion `dlist_empty(&memhooks_monitor->list)' failed.
    return 0;
}
