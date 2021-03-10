//
// Created by depaulsmiller on 2/23/21.
//

#include <atomic>
#include <networklayer/connectionless.hh>
#include <gtest/gtest.h>
#include <future>

TEST(connectionlessTest, connectionlessTest_send_recv) {
    spdlog::set_level(spdlog::level::trace); // This setting is missed in the wiki

    std::atomic_bool done;

    done = false;

    auto f = std::async([&done]() {
        done = true;
        const char *address = "127.0.0.1";
        cse498::ConnectionlessServer f(address, 8080);
        char *buf = new char[4096];
        fid_mr *mr;
        f.registerMR(buf, 4096, mr);
        fi_addr_t addr;
        f.recv_addr(buf, 4096, addr);
        buf[0] = 'a';
        buf[1] = '\0';
        f.send(addr, buf, 4096);
        ERRCHK(fi_close(&(mr->fid)));
    });

    while (!done);

    std::string addr = "127.0.0.1";
    cse498::ConnectionlessClient c(addr.c_str(), 8080);
    char *buf = new char[4096];
    fid_mr *mr;

    c.registerMR(buf, 4096, mr);
    c.send_addr(buf, 4096);
    c.recv(buf, 4096);

    f.get();
    ERRCHK(fi_close(&(mr->fid)));

}

TEST(connectionlessTest, connectionlessTest_send_recv_multiple_clients) {
    spdlog::set_level(spdlog::level::trace); // This setting is missed in the wiki

    std::atomic_bool done;

    done = false;

    auto f = std::async([&done]() {
        done = true;
        const char *address = "127.0.0.1";
        cse498::ConnectionlessServer f(address, 8080);
        char *buf = new char[4096];
        fid_mr *mr;
        f.registerMR(buf, 4096, mr);
        fi_addr_t addr, addr2;
        f.recv_addr(buf, 4096, addr);
        f.recv_addr(buf, 4096, addr2);

        buf[0] = 'a';
        buf[1] = '\0';
        f.send(addr, buf, 4096);
        f.send(addr2, buf, 4096);
        ERRCHK(fi_close(&(mr->fid)));
    });

    while (!done);

    auto f2 = std::async([] {
        const char *addr = "127.0.0.1";
        cse498::ConnectionlessClient c(addr, 8080);
        char *buf = new char[4096];
        fid_mr *mr;

        c.registerMR(buf, 4096, mr);
        c.send_addr(buf, 4096);
        c.recv(buf, 4096);

        ERRCHK(fi_close(&(mr->fid)));
    });

    const char *addr = "127.0.0.1";
    cse498::ConnectionlessClient c(addr, 8080);
    char *buf = new char[4096];
    fid_mr *mr;

    c.registerMR(buf, 4096, mr);
    c.send_addr(buf, 4096);
    c.recv(buf, 4096);

    f.get();
    f2.get();
    ERRCHK(fi_close(&(mr->fid)));

}
