//
// Created by depaulsmiller on 2/23/21.
//

#include <networklayer/RPC.hh>
#include <networklayer/fabricBased.hh>
#include <gtest/gtest.h>
#include <future>

void registerReturnPackAs1(cse498::FabricRPC &f);

TEST(fabricTest, fabricTest_echo) {
    spdlog::set_level(spdlog::level::debug); // This setting is missed in the wiki

    std::atomic_bool done;
    done = false;

    auto f = std::async([&done]() {
        done = true;
        const char *address = "127.0.0.1";
        cse498::FabricRPC f(address);
        registerReturnPackAs1(f);
        f.start();
    });

    while (!done);

    std::string addr = "127.0.0.1";
    cse498::FabricRPClient c(addr, cse498::DEFAULT_PORT);

    auto res = c.callRemote(1, cse498::pack_t(addr.begin(), addr.end()));
    std::string s(res.begin(), res.end());

    ASSERT_TRUE(addr == s);

    c.callRemote(0, cse498::pack_t(addr.begin(), addr.end()));

    f.get();

}
