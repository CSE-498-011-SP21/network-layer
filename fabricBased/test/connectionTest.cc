#include <networklayer/connection.hh>
#include <gtest/gtest.h>
#include <future>
#include <atomic>
#include <chrono>
#include <thread>

TEST(connectionTest, connection_async_send_recv) {
    std::atomic_bool listening = false;
    const std::string msg = "potato\0";

    auto f = std::async([&msg, &listening]() {
        // c1 stuff
        const char* addr = "127.0.0.1";

        cse498::Connection *c1 = new cse498::Connection(addr, [&listening]() { listening = true; });
        c1->async_send(msg.c_str(), msg.length() + 1);
        c1->wait_for_sends();
    });

    while (!listening);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1");

    char *buf = new char[128];
    c2->wait_recv(buf, 128);
    ASSERT_STREQ(msg.c_str(), buf);
}

TEST(connectionTest, connection_wait_send_recv_response) {
    std::atomic_bool listening = false;
    const std::string msg = "potato\0";
    const std::string msg_res = "potato\0";

    auto f = std::async([&msg, &msg_res, &listening]() {
        // c1 stuff
        const char* addr = "127.0.0.1";

        cse498::Connection *c1 = new cse498::Connection(addr, [&listening]() { listening = true; });
        c1->wait_send(msg.c_str(), msg.length() + 1);

        char *buf = new char[128];
        c1->wait_recv(buf, 128);
        ASSERT_STREQ(msg_res.c_str(), buf);
    });

    while (!listening);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1");

    char *buf = new char[128];
    c2->wait_recv(buf, 128);
    ASSERT_STREQ(msg.c_str(), buf);
    c2->wait_send(msg_res.c_str(), msg_res.length() + 1);
}

TEST(connectionTest, connection_send_recv_multiple_connections) {
    std::atomic_bool c0_listening_for_c1 = false;
    std::atomic_bool c0_listening_for_c2 = false;
    std::atomic_bool c1_connected = false;
    const std::string c0_to_c1_msg = "Hi c1!\0";
    const std::string c0_to_c2_msg = "Howdy c2!\0";

    auto f = std::async([&c0_to_c1_msg, &c0_to_c2_msg, &c0_listening_for_c1, &c0_listening_for_c2]() {
        // c0 stuff
        const char* addr = "127.0.0.1";

        // c1's connection to c0
        cse498::Connection *c0_c1 = new cse498::Connection(addr, [&c0_listening_for_c1]() { c0_listening_for_c1 = true; });
        // c2's connection to c0
        cse498::Connection *c0_c2 = new cse498::Connection(addr, [&c0_listening_for_c2]() { c0_listening_for_c2 = true; });


        c0_c1->async_send(c0_to_c1_msg.c_str(), c0_to_c1_msg.length() + 1);
        c0_c2->async_send(c0_to_c2_msg.c_str(), c0_to_c2_msg.length() + 1);
        c0_c1->wait_for_sends();
        c0_c2->wait_for_sends();
    });

    auto f2 = std::async([&c0_to_c1_msg, &c0_listening_for_c1]() {
        // c1 stuff
        while (!c0_listening_for_c1);
        cse498::Connection *c1 = new cse498::Connection("127.0.0.1");

        char *buf = new char[128];
        c1->wait_recv(buf, 128);
        ASSERT_STREQ(c0_to_c1_msg.c_str(), buf);
    });

    while (!c0_listening_for_c2);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1");

    char *buf = new char[128];
    c2->wait_recv(buf, 128);
    ASSERT_STREQ(c0_to_c2_msg.c_str(), buf);
}

TEST(connectionTest, connection_rma) {

    LOG_LEVEL = DEBUG;

    std::atomic_bool listening = false;
    std::atomic_bool done = false;
    std::atomic_bool done2 = false;

    std::atomic_bool latch = false;
    volatile char *remoteAccess = new char[sizeof(uint64_t)];

    auto f = std::async([&listening, &done, &done2, &latch, &remoteAccess]() {
        // c1 stuff
        const char* addr = "127.0.0.1";
        cse498::Connection *c1 = new cse498::Connection(addr, [&listening]() { listening = true; });

        *((uint64_t *) remoteAccess) = ~0;
        c1->register_mr((char *) remoteAccess, sizeof(uint64_t), FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ,
                        1);

        latch = true;
        while (!done);
        while (!done2);

        ASSERT_TRUE(*((uint64_t *) remoteAccess) == 0);
    });

    while (!listening);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1");

    char *buf = new char[sizeof(uint64_t)];
    *((uint64_t *) buf) = 10;
    while (!latch);
    c2->wait_read(buf, sizeof(uint64_t), 0, 1);
    std::cerr << "Read: " << *(uint64_t *) buf << std::endl;
    ASSERT_TRUE(*((uint64_t *) buf) == ~0);

    done = true;

    *((uint64_t *) buf) = 0;
    c2->wait_write(buf, sizeof(uint64_t), 0, 1);
    done2 = true;


    f.get();
}
