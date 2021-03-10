//
// Created by depaulsmiller on 2/18/21.
//

#include <networklayer/fabricBased.hh>
#include <thread>

int main(int argc, char **argv) {
    spdlog::set_level(spdlog::level::trace);

    std::string addr = "127.0.0.1";
    if (argc > 1) {
        addr = std::string(argv[1]);
    }

    spdlog::debug("Using addr {}", addr);

    cse498::FabricRPClient c(addr, cse498::DEFAULT_PORT);
    std::string s = "hi";
    cse498::pack_t p(s.begin(), s.end());

    for (int i = 0; i < 10; i++) {
        p = c.callRemote(1, p);
        s = std::string(p.begin(), p.end());
        std::cout << s << std::endl;
    }

    c.callRemote(0, p);

    return 0;
}