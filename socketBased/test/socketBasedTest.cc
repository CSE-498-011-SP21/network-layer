//
// Created by depaulsmiller on 2/18/21.
//

#include <socketBased.hh>

int main(int argc, char **argv) {
    cse498::SocketRPC rpc;
    rpc.registerRPC(0, [](cse498::pack_t p){
        return p;
    });

    cse498::SocketRPClient client("127.0.0.1", cse498::DEFAULT_PORT);
    std::string s = "hi";
    cse498::pack_t p(s.begin(), s.end());

    auto res = client.callRemote(0, p);

    s = std::string(res.begin(), res.end());

    std::cout << s << std::endl;

    return 0;
}