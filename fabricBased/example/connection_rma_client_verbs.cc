//
// Created by depaulsmiller on 3/17/21.
//

#include <networklayer/connection.hh>

int LOG_LEVEL = DEBUG;

int main(int argc, char** argv){
    
    std::string addr = "127.0.0.1";

    if(argc > 1){
        addr = argv[1];
    }
    int port = 8080;
    auto *c2 = new cse498::Connection(addr.c_str(), false, port, FI_PROTO_RDMA_CM_IB_RC);

    cse498::unique_buf buf;

    c2->register_mr(buf, FI_READ | FI_WRITE, 1);

    std::cerr << "Recv" << std::endl;

    c2->recv(buf, 1);

    *((uint64_t *) buf.get()) = 10;

    c2->read(buf, sizeof(uint64_t), 0, 1);

    while ((*(uint64_t *) buf.get()) != ~0);

    std::cerr << "Read: " << *(uint64_t *) buf.get() << std::endl;

    std::cerr << "Send" << std::endl;

    c2->send(buf, 1);
}
