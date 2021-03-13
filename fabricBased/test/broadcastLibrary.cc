//
// Created by depaulsmiller on 3/10/21.
//

#include <networklayer/connectionless.hh>
#include <networklayer/connection.hh>

int LOG_LEVEL = TRACE;

void rbc(cse498::ConnectionlessServer &c, const std::vector<cse498::addr_t> &addresses, char *message,
         size_t messageSize) {
    reliableBroadcast(c, addresses, message, messageSize);
}

// void rbc(const std::vector<cse498::Connection> &connections, char *message, size_t messageSize) {
//     reliableBroadcast(connections, message, messageSize);
// }