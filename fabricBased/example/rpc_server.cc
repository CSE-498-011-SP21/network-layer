//
// Created by depaulsmiller on 2/18/21.
//

#include <networklayer/fabricBased.hh>
#include <thread>

int LOG_LEVEL = TRACE;

int main() {
    //spdlog::set_level(spdlog::level::trace);

    cse498::FabricRPC f(nullptr);
    f.registerRPC(1, [](cse498::pack_t p) {
        return p;
    });

    f.start();

    return 0;
}