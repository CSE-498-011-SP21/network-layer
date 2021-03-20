//
// Created by depaulsmiller on 3/17/21.
//

#include <networklayer/connection.hh>

int LOG_LEVEL = DEBUG;

int main(){

    cse498::Connection *c2 = new cse498::Connection("127.0.0.1");

    char *buf = new char[sizeof(uint64_t)];

    std::cerr << "Recv" << std::endl;

    c2->wait_recv(buf, 1);

    *((uint64_t *) buf) = 10;

    c2->wait_read(buf, sizeof(uint64_t), 0, 1);

    while ((*(uint64_t *) buf) != ~0);

    std::cerr << "Read: " << *(uint64_t *) buf << std::endl;

    std::cerr << "Send" << std::endl;

    c2->wait_send(buf, 1);

}