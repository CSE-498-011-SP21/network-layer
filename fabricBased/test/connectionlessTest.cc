//
// Created by depaulsmiller on 2/23/21.
//

#include <atomic>
#include <networklayer/connectionless.hh>
#include <gtest/gtest.h>
#include <future>

void rbc(cse498::ConnectionlessServer &c, const std::vector<cse498::addr_t> &addresses, char *message,
         size_t messageSize);

TEST(connectionlessTest, connectionlessTest_send_recv) {
    //spdlog::set_level(spdlog::level::trace); // This setting is missed in the wiki

    std::atomic_bool done;

    done = false;

    auto f = std::async([&done]() {
        const char *address = "127.0.0.1";
        cse498::ConnectionlessServer f(address, 8080);
        char *buf = new char[4096];
        fid_mr *mr;
        f.registerMR(buf, 4096, mr);
        cse498::addr_t addr;
        f.async_accept(buf, 4096);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        done = true;
        addr = f.wait_accept(buf, 4096);
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
    c.connect(buf, 4096);
    c.recv(buf, 4096);

    f.get();
    ERRCHK(fi_close(&(mr->fid)));

}

TEST(connectionlessTest, connectionlessTest_send_recv_retry) {
    //spdlog::set_level(spdlog::level::trace); // This setting is missed in the wiki

    std::atomic_bool done;

    done = false;

    auto f = std::async([&done]() {
        done = true;
        const char *address = "127.0.0.1";
        cse498::ConnectionlessServer f(address, 8080);
        char *buf = new char[4096];
        fid_mr *mr;
        f.registerMR(buf, 4096, mr);
        cse498::addr_t addr = f.accept(buf, 4096);
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

    while (!c.async_connect(buf, 4096)) {
        DO_LOG(TRACE) << "Need to retry making connection";
    }

    c.wait_connect(buf, 4096);

    c.recv(buf, 4096);

    f.get();
    ERRCHK(fi_close(&(mr->fid)));

}


TEST(connectionlessTest, connectionlessTest_send_recv_multiple_clients) {
    //spdlog::set_level(spdlog::level::trace); // This setting is missed in the wiki

    std::atomic_bool done, done2;

    done = false;
    done2 = false;

    auto f = std::async([&done, &done2]() {
        const char *address = "127.0.0.1";
        cse498::ConnectionlessServer f(address, 8080);
        char *buf = new char[4096];
        fid_mr *mr;
        f.registerMR(buf, 4096, mr);
        fi_addr_t addr, addr2;
        f.async_accept(buf, 4096);
        done = true;
        addr = f.wait_accept(buf, 4096);
        f.async_accept(buf, 4096);
        f.send(addr, buf, 4096);
        done2 = true;
        addr2 = f.wait_accept(buf, 4096);
        buf[0] = 'a';
        buf[1] = '\0';
        f.send(addr2, buf, 4096);
        ERRCHK(fi_close(&(mr->fid)));
    });

    while (!done);

    auto f2 = std::async([&done2] {
        const char *addr = "127.0.0.1";
        cse498::ConnectionlessClient c(addr, 8080);
        char *buf = new char[4096];
        fid_mr *mr;

        c.registerMR(buf, 4096, mr);
        while (!done2);
        c.connect(buf, 4096);
        c.recv(buf, 4096);

        ERRCHK(fi_close(&(mr->fid)));
    });

    const char *addr = "127.0.0.1";
    cse498::ConnectionlessClient c(addr, 8080);
    char *buf = new char[4096];
    fid_mr *mr;

    c.registerMR(buf, 4096, mr);
    c.connect(buf, 4096);
    c.recv(buf, 4096);

    f.get();
    f2.get();
    ERRCHK(fi_close(&(mr->fid)));

}

TEST(connectionlessTest, connectionlessTest_broadcast) {
    //spdlog::set_level(spdlog::level::trace); // This setting is missed in the wiki

    std::atomic_bool done;

    done = false;

    auto f = std::async([&done]() {
        const char *address = "127.0.0.1";
        cse498::ConnectionlessServer f(address, 8080);
        char *buf = new char[4096];
        fid_mr *mr;
        f.registerMR(buf, 4096, mr);
        fi_addr_t addr;
        done = true;
        addr = f.accept(buf, 4096);
        buf[0] = 'a';
        buf[1] = '\0';
        rbc(f, {addr}, buf, 4096);
        ERRCHK(fi_close(&(mr->fid)));
    });

    while (!done);

    std::string addr = "127.0.0.1";
    cse498::ConnectionlessClient c(addr.c_str(), 8080);
    char *buf = new char[4096];
    fid_mr *mr;

    c.registerMR(buf, 4096, mr);
    c.connect(buf, 4096);
    std::vector<cse498::ConnectionlessClient> v;
    cse498::reliableBroadcastReceiveFrom(c, v, buf, 4096, [](char *c, size_t s) { return true; },
                                         [](char *c, size_t s) {});

    f.get();
    ERRCHK(fi_close(&(mr->fid)));

}
