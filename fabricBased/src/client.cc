//
// Created by depaulsmiller on 2/18/21.
//

#include <fabricBased.hh>
#include <thread>

int main(){
    std::string addr = "127.0.0.1";
    cse498::FabricRPClient c(addr, cse498::DEFAULT_PORT);
    std::string s = "hi";
    cse498::pack_t p(s.begin(), s.end());

    for(int i = 0; i < 10; i++) {
        p = c.callRemote(1, p);
        s = std::string(p.begin(), p.end());
        std::cout << s << std::endl;
    }

    c.callRemote(0, p);

    return 0;
}