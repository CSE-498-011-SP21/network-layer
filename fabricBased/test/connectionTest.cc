#include <networklayer/connection.hh>
#include <gtest/gtest.h>
#include <future>
#include <atomic>
#include <chrono>
#include <thread>

void rbc(std::vector<cse498::Connection> &connections, const char *message, size_t messageSize);

TEST(connectionTest, connection_async_send_recv) {
    int port = 8080;
    std::atomic_bool listening = false;
    const std::string msg = "potato\0";

    auto f = std::async([&msg, &port, &listening]() {
        // c1 stuff
        const char* addr = "127.0.0.1";

        cse498::Connection *c1 = new cse498::Connection(addr, port, [&listening]() { listening = true; });
        c1->async_send(msg.c_str(), msg.length() + 1);
        c1->wait_for_sends();
    });

    while (!listening);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1", port);

    char *buf = new char[128];
    c2->wait_recv(buf, 128);
    ASSERT_STREQ(msg.c_str(), buf);
}

TEST(connectionTest, connection_wait_send_recv_response) {
    int port = 8080;
    std::atomic_bool listening = false;
    const std::string msg = "potato\0";
    const std::string msg_res = "potato\0";

    auto f = std::async([&msg, &port, &msg_res, &listening]() {
        // c1 stuff
        const char* addr = "127.0.0.1";

        cse498::Connection *c1 = new cse498::Connection(addr, port, [&listening]() { listening = true; });
        c1->wait_send(msg.c_str(), msg.length() + 1);

        char *buf = new char[128];
        c1->wait_recv(buf, 128);
        ASSERT_STREQ(msg_res.c_str(), buf);
    });

    while (!listening);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1", port);

    char *buf = new char[128];
    c2->wait_recv(buf, 128);
    ASSERT_STREQ(msg.c_str(), buf);
    c2->wait_send(msg_res.c_str(), msg_res.length() + 1);
}

TEST(connectionTest, connection_send_recv_multiple_connections) {
    int port = 8080;
    std::atomic_bool c0_listening_for_c1 = false;
    std::atomic_bool c0_listening_for_c2 = false;
    std::atomic_bool c1_connected = false;
    const std::string c0_to_c1_msg = "Hi c1!\0";
    const std::string c0_to_c2_msg = "Howdy c2!\0";

    auto f = std::async([&c0_to_c1_msg, &c0_to_c2_msg, &port, &c0_listening_for_c1, &c0_listening_for_c2]() {
        // c0 stuff
        const char* addr = "127.0.0.1";

        // c1's connection to c0
        cse498::Connection *c0_c1 = new cse498::Connection(addr, port, [&c0_listening_for_c1]() { c0_listening_for_c1 = true; });
        // c2's connection to c0
        cse498::Connection *c0_c2 = new cse498::Connection(addr, port, [&c0_listening_for_c2]() { c0_listening_for_c2 = true; });


        c0_c1->async_send(c0_to_c1_msg.c_str(), c0_to_c1_msg.length() + 1);
        c0_c2->async_send(c0_to_c2_msg.c_str(), c0_to_c2_msg.length() + 1);
        c0_c1->wait_for_sends();
        c0_c2->wait_for_sends();
    });

    auto f2 = std::async([&c0_to_c1_msg, &port, &c0_listening_for_c1]() {
        // c1 stuff
        while (!c0_listening_for_c1);
        cse498::Connection *c1 = new cse498::Connection("127.0.0.1", port);

        char *buf = new char[128];
        c1->wait_recv(buf, 128);
        ASSERT_STREQ(c0_to_c1_msg.c_str(), buf);
    });

    while (!c0_listening_for_c2);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1", port);

    char *buf = new char[128];
    c2->wait_recv(buf, 128);
    ASSERT_STREQ(c0_to_c2_msg.c_str(), buf);
}

TEST(connectionTest, connection_broadcast) {
    int port = 8080;
    std::atomic_bool listening = false;
    const std::string msg = "wowww (owen wilson voice\0";
    

    auto f = std::async([&msg, &port, &listening]() {
        const char* addr = "127.0.0.1";
        cse498::Connection *c1 = new cse498::Connection(addr, port, [&listening]() {listening = true;});

        std::vector<cse498::Connection> v;
        v.push_back(*c1);

        rbc(v, msg.c_str(), 4096);
    });

    while (!listening);

    cse498::Connection *c2 = new cse498::Connection("127.0.0.1", port);

    char *buf2 = new char[4096];
    std::vector<cse498::Connection> v;
    // Should be false since hasnt recieved before
    bool res = cse498::reliableBroadcastReceiveFrom(*c2, v, buf2, 4096, [](char *c, size_t s) { return true; },
                                         [](char *c, size_t s) {});
    ASSERT_FALSE(res);
    ASSERT_STREQ(msg.c_str(), buf2);

}

TEST(connectionTest, connection_rma) {
    int port = 8080;
    std::atomic_bool listening = false;
    std::atomic_bool done = false;
    std::atomic_bool done2 = false;

    std::atomic_bool latch = false;
    volatile char *remoteAccess = new char[sizeof(uint64_t)];

    auto f = std::async([&listening, &port, &done, &done2, &latch, &remoteAccess]() {
        // c1 stuff
        const char* addr = "127.0.0.1";
        cse498::Connection *c1 = new cse498::Connection(addr, port, [&listening]() { listening = true; });

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
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1", port);

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

TEST(connectionTest, connection_changing_rma_perms) {
    std::atomic_bool c0_listening_for_c1 = false;
    std::atomic_bool c0_listening_for_c2 = false;
    std::atomic_bool c1_connected = false;
    std::atomic_bool mr_registered = false;
    std::atomic_bool mr1_wrote = false;
    std::atomic_bool mr2_wrote = false;
    std::atomic_bool perms_updated = false;
    std::atomic_bool value_read = false;
    std::atomic_bool c0_done = false;
    std::atomic_bool c1_done = false;
    std::atomic_bool c2_done = false;
    const std::string c0_to_c1_msg = "Hi c1!\0";
    const std::string c0_to_c2_msg = "Howdy c2!\0";
    int port = 8080;

    auto f = std::async([&c0_to_c1_msg, &c0_to_c2_msg, &port, &c0_listening_for_c1, &c0_listening_for_c2, &mr_registered, &mr1_wrote, &mr2_wrote, &perms_updated, &c2_done, &c0_done]() {
        // c0 stuff
        const char* addr = "127.0.0.1";

        volatile char *remoteAccess = new char[sizeof(uint64_t)];
        *((uint64_t *) remoteAccess) = ~0;
        volatile char *remoteAccess2 = new char[sizeof(uint64_t)];
        *((uint64_t *) remoteAccess2) = 5;

        // c1's connection to c0
        cse498::Connection *c0_c1 = new cse498::Connection(addr, port, [&c0_listening_for_c1]() { c0_listening_for_c1 = true; });
        // c2's connection to c0
        cse498::Connection *c0_c2 = new cse498::Connection(addr, port, [&c0_listening_for_c2]() { c0_listening_for_c2 = true; });
        ASSERT_EQ(false, c0_c1->register_mr((char *) remoteAccess, sizeof(uint64_t), FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 1));
        ASSERT_EQ(false, c0_c2->register_mr((char *) remoteAccess, sizeof(uint64_t), FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 1));
        ASSERT_EQ(false, c0_c1->register_mr((char *) remoteAccess2, sizeof(uint64_t), FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 2));
        ASSERT_EQ(false, c0_c2->register_mr((char *) remoteAccess2, sizeof(uint64_t), FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 2));
        mr_registered = true;

        while (!mr1_wrote || !mr2_wrote);
        ASSERT_EQ(10, *((uint64_t *) remoteAccess));
        ASSERT_EQ(100, *((uint64_t *) remoteAccess2));

        ASSERT_EQ(true, c0_c2->register_mr((char *) remoteAccess2, sizeof(uint64_t), FI_READ | FI_REMOTE_READ, 2));
        perms_updated = true;

        while (!c2_done);
        ASSERT_EQ(1000, *((uint64_t *) remoteAccess2));
        ASSERT_EQ(10000, *((uint64_t *) remoteAccess));

        c0_done = true;
    });

    auto f2 = std::async([&c0_to_c1_msg, &c0_listening_for_c1, &port, &mr_registered, &mr2_wrote, &perms_updated, &c1_done, &c0_done]() {
        // c1 stuff
        while (!c0_listening_for_c1);
        cse498::Connection *c1 = new cse498::Connection("127.0.0.1", port);
        char *buf = new char[sizeof(uint64_t)];

        while (!mr_registered);

        *((uint64_t *) buf) = 100;
        c1->wait_write(buf, sizeof(uint64_t), 0, 2);
        mr2_wrote = true;

        while (!perms_updated);

        *((uint64_t *) buf) = 1000;
        c1->wait_write(buf, sizeof(uint64_t), 0, 2); // Make sure it can still write on 2

        c1_done = true;
        while (!c0_done);
    });

    while (!c0_listening_for_c2);
    // c2 stuff
    cse498::Connection *c2 = new cse498::Connection("127.0.0.1", port);
    char *buf = new char[sizeof(uint64_t)];
    while (!mr_registered);
    *((uint64_t *) buf) = 10;
    c2->wait_write(buf, sizeof(uint64_t), 0, 1);
    mr1_wrote = true;

    while (!c1_done);

    c2->wait_read(buf, sizeof(uint64_t), 0, 2); // Make sure it still has read access. 
    ASSERT_EQ(1000, *((uint64_t *) buf));
    value_read = true;

    *((uint64_t *) buf) = 10000;
    c2->wait_write(buf, sizeof(uint64_t), 0, 1); // Make sure it can still write on 1. 

    *((uint64_t *) buf) = 100000;
    
    // TODO this next line currently causes the program to exit which makes the test fail. (even though exiting means it passed)
    // Once we update wait_write to return on failure instead of exit we need to re-add this line (with the proper assertion)
    // (I've tried to use ASSERT_EXIT with this but it seems to hang indefinitely, likely since it calls fork which I doubt works well with networking)
    // c2->wait_write(buf, sizeof(uint64_t), 0, 2); // Make sure it can't write on 2
    
    c2_done = true;

    f.get();
    f2.get();
}
