#include <networklayer/connection.hh>
#include <gtest/gtest.h>
#include <future>
#include <atomic>
#include <chrono>
#include <thread>

void rbc(std::vector<cse498::Connection> &connections, cse498::unique_buf& message, size_t messageSize);

TEST(connectionTest, connection_async_send_recv) {
    const std::string msg = "potato\0";

    auto f = std::async([&msg]() {
        // c1 stuff
        auto *c1 = new cse498::Connection("127.0.0.1", false);

        cse498::unique_buf buf;

        buf.cpyTo(msg.c_str(), msg.length() + 1);
        c1->register_mr(buf, FI_WRITE | FI_READ, 1);
        c1->async_send(buf, msg.length() + 1);
        c1->wait_for_sends();
        delete c1;
    });

    // c2 stuff
    auto *c2 = new cse498::Connection("127.0.0.1", true);

    cse498::unique_buf buf;
    c2->register_mr(buf, FI_WRITE | FI_READ, 1);
    c2->recv(buf, 128);
    ASSERT_STREQ(msg.c_str(), buf.get());
    f.get();
    delete c2;
}

TEST(connectionTest, connection_try_recv) {
    const std::string msg = "try_potato\0";

    auto f = std::async([&msg]() {
        // c1 stuff
        auto *c1 = new cse498::Connection("127.0.0.1", false);

        cse498::unique_buf buf;
        c1->register_mr(buf, FI_WRITE | FI_READ, 1);

        buf.cpyTo(msg.c_str(), msg.length() + 1);

        c1->async_send(buf, msg.length() + 1);
        c1->wait_for_sends();
        delete c1;
    });

    auto *c2 = new cse498::Connection("127.0.0.1", true);

    cse498::unique_buf buf;
    c2->register_mr(buf, FI_WRITE | FI_READ, 1);

    while (!c2->try_recv(buf, 128));
    ASSERT_STREQ(msg.c_str(), buf.get());
    f.get();
    delete c2;
}

TEST(connectionTest, connection_wait_send_recv_response) {
    const std::string msg = "potato\0";
    const std::string msg_res = "potato\0";

    auto f = std::async([&msg, &msg_res]() {
        // c1 stuff
        const char *addr = "127.0.0.1";

        auto *c1 = new cse498::Connection(addr, true);

        cse498::unique_buf buf;
        c1->register_mr(buf, FI_WRITE | FI_READ, 1);

        buf.cpyTo(msg.c_str(), msg.length() + 1);

        c1->send(buf, msg.length() + 1);

        c1->recv(buf, 128);
        ASSERT_STREQ(msg_res.c_str(), buf.get());
        delete c1;
    });

    // c2 stuff

    auto *c2 = new cse498::Connection("127.0.0.1", false);

    cse498::unique_buf buf;
    c2->register_mr(buf, FI_WRITE | FI_READ, 1);

    c2->recv(buf, 128);
    ASSERT_STREQ(msg.c_str(), buf.get());
    buf.cpyTo(msg_res.c_str(), msg_res.length() + 1);
    c2->send(buf, msg_res.length() + 1);
    f.get();
    delete c2;
}

TEST(connectionTest, connection_send_recv_multiple_connections) {
    std::atomic_bool c1_connected;
    c1_connected = false;
    const std::string c0_to_c1_msg = "Hi c1!\0";
    const std::string c0_to_c2_msg = "Howdy c2!\0";

    auto f = std::async([&c0_to_c1_msg, &c0_to_c2_msg]() {
        // c0 stuff
        const char *addr = "127.0.0.1";

        // c1's connection to c0
        auto *c0_c1 = new cse498::Connection(addr, true);
        // c2's connection to c0
        auto *c0_c2 = new cse498::Connection(addr, true);

        cse498::unique_buf c0_to_c1_msg_buf, c0_to_c2_msg_buf;
        c0_c1->register_mr(c0_to_c1_msg_buf, FI_WRITE | FI_READ, 1);
        c0_c2->register_mr(c0_to_c2_msg_buf, FI_WRITE | FI_READ, 2);

        c0_to_c1_msg_buf = c0_to_c1_msg;
        c0_to_c2_msg_buf = c0_to_c2_msg;

        c0_c1->async_send(c0_to_c1_msg_buf, c0_to_c1_msg.length() + 1);
        c0_c2->async_send(c0_to_c2_msg_buf, c0_to_c2_msg.length() + 1);
        c0_c1->wait_for_sends();
        c0_c2->wait_for_sends();
        delete c0_c1, c0_c2;
    });

    auto f2 = std::async([&c0_to_c1_msg, &c1_connected]() {
        // c1 stuff
        auto *c1 = new cse498::Connection("127.0.0.1", false);

        c1_connected = true;

        cse498::unique_buf buf;
        c1->register_mr(buf, FI_WRITE | FI_READ, 1);

        c1->recv(buf, 128);
        ASSERT_STREQ(c0_to_c1_msg.c_str(), buf.get());
        delete c1;
    });

    while (!c1_connected);
    // c2 stuff
    auto *c2 = new cse498::Connection("127.0.0.1", false);

    cse498::unique_buf buf;
    c2->register_mr(buf, FI_WRITE | FI_READ, 1);

    c2->recv(buf, 128);
    ASSERT_STREQ(c0_to_c2_msg.c_str(), buf.get());
    f2.get();
    f.get();
    delete c2;
}

TEST(connectionTest, connection_broadcast) {
    const std::string msg = "wowww (owen wilson voice)\0";
    const std::string resp = "you're weird\0";

    auto f = std::async([&msg, &resp]() {
        const char *addr = "127.0.0.1";
        cse498::Connection c1 = cse498::Connection(addr, false);
        cse498::unique_buf buf, buf3;
        c1.register_mr(buf, FI_WRITE | FI_READ, 1);
        c1.register_mr(buf3, FI_WRITE | FI_READ, 2);

        std::vector<cse498::Connection> v;
        v.push_back(std::move(c1));

        buf3 = msg;

        rbc(v, buf3, 4096);

        v[0].recv(buf, 4096);
        ASSERT_STREQ(resp.c_str(), buf.get());
    });

    auto *c2 = new cse498::Connection("127.0.0.1", true);

    cse498::unique_buf buf2;
    c2->register_mr(buf2, FI_WRITE | FI_READ, 1);
    std::vector<cse498::Connection> v = {};
    // Should be false since hasnt recieved before
    bool res = cse498::reliableBroadcastReceiveFrom(*c2, v, buf2,
                                                    [](const cse498::unique_buf &b, size_t s) { return false; },
                                                    [](const cse498::unique_buf &b, size_t s) {});
    ASSERT_TRUE(res);
    ASSERT_STREQ(msg.c_str(), buf2.get());

    buf2 = resp;

    c2->send(buf2, resp.length() + 1);
    f.get();
    delete c2;
}

TEST(connectionTest, connection_rma) {
    std::atomic_bool done;
    done = false;

    std::atomic_bool latch;
    latch = false;

    cse498::unique_buf remoteAccess, buf;

    auto f = std::async([&done, &latch, &remoteAccess]() {
        // c1 stuff
        const char *addr = "127.0.0.1";
        auto *c1 = new cse498::Connection(addr, true);

        *((uint64_t *) remoteAccess.get()) = ~0;
        c1->register_mr(remoteAccess, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ,
                        1);

        latch = true;
        while (!done);

        ASSERT_TRUE(*((uint64_t *) remoteAccess.get()) == 0);
        delete c1;
    });

    // c2 stuff

    auto *c2 = new cse498::Connection("127.0.0.1", false);
    c2->register_mr(buf, FI_WRITE | FI_READ, 1);

    *((uint64_t *) buf.get()) = 10;
    while (!latch);
    c2->read(buf, sizeof(uint64_t), 0, 1);
    std::cerr << "Read: " << *(uint64_t *) buf.get() << std::endl;
    ASSERT_TRUE(*((uint64_t *) buf.get()) == ~0);


    *((uint64_t *) buf.get()) = 0;
    c2->write(buf, sizeof(uint64_t), 0, 1);
    done = true;

    f.get();

    delete c2;
}

TEST(connectionTest, connection_rma_try_read) {
    std::atomic_bool done;
    done = false;

    std::atomic_bool latch;
    latch = false;
    cse498::unique_buf remoteAccess, buf;

    auto f = std::async([&done, &latch, &remoteAccess]() {
        // c1 stuff
        const char *addr = "127.0.0.1";
        auto *c1 = new cse498::Connection(addr, true);

        *((uint64_t *) remoteAccess.get()) = ~0;
        c1->register_mr(remoteAccess, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ,
                        1);

        latch = true;
        while (!done);

        ASSERT_TRUE(*((uint64_t *) remoteAccess.get()) == 0);
        delete c1;
    });

    auto *c2 = new cse498::Connection("127.0.0.1", false);
    c2->register_mr(buf, FI_WRITE | FI_READ, 1);

    *((uint64_t *) buf.get()) = 10;
    while (!latch);
    while (!c2->try_read(buf, sizeof(uint64_t), 0, 1));
    //c2->wait_read(buf, sizeof(uint64_t), 0, 1);
    std::cerr << "Read: " << *(uint64_t *) buf.get() << std::endl;
    ASSERT_TRUE(*((uint64_t *) buf.get()) == ~0);

    *((uint64_t *) buf.get()) = 0;
    c2->write(buf, sizeof(uint64_t), 0, 1);
    done = true;
    f.get();

    delete c2;
}

TEST(connectionTest, connection_changing_rma_perms) {
    std::atomic_bool c1_connected;
    c1_connected = false;
    std::atomic_bool mr_registered;
    mr_registered = false;
    std::atomic_bool mr1_wrote;
    mr1_wrote = false;
    std::atomic_bool mr2_wrote;
    mr2_wrote = false;
    std::atomic_bool perms_updated;
    perms_updated = false;
    std::atomic_bool c0_done;
    c0_done = false;
    std::atomic_bool c1_done;
    c1_done = false;
    std::atomic_bool c2_done;
    c2_done = false;

    auto f = std::async([&mr_registered, &mr1_wrote, &mr2_wrote, &perms_updated, &c2_done, &c0_done]() {
        // c0 stuff
        const char *addr = "127.0.0.1";

        cse498::unique_buf remoteAccess, remoteAccess2;
        *((uint64_t *) remoteAccess.get()) = ~0;
        *((uint64_t *) remoteAccess2.get()) = 5;

        // c1's connection to c0
        auto *c0_c1 = new cse498::Connection(addr, true);
        // c2's connection to c0
        auto *c0_c2 = new cse498::Connection(addr, true);
        ASSERT_EQ(false, c0_c1->register_mr(remoteAccess,
                                            FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 1));
        ASSERT_EQ(false, c0_c2->register_mr(remoteAccess,
                                            FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 1));
        ASSERT_EQ(false, c0_c1->register_mr(remoteAccess2,
                                            FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 2));
        ASSERT_EQ(false, c0_c2->register_mr(remoteAccess2,
                                            FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 2));
        mr_registered = true;

        while (!mr1_wrote || !mr2_wrote);
        ASSERT_EQ(10, *((uint64_t *) remoteAccess.get()));
        ASSERT_EQ(100, *((uint64_t *) remoteAccess2.get()));

        ASSERT_EQ(true, c0_c2->register_mr(remoteAccess2, FI_READ | FI_REMOTE_READ, 2));
        perms_updated = true;

        while (!c2_done);
        ASSERT_EQ(1000, *((uint64_t *) remoteAccess2.get()));
        ASSERT_EQ(10000, *((uint64_t *) remoteAccess.get()));

        c0_done = true;

        delete c0_c1, c0_c2;
    });

    auto f2 = std::async([&c1_connected, &mr_registered, &mr2_wrote, &perms_updated, &c1_done, &c0_done]() {
        // c1 stuff
        auto *c1 = new cse498::Connection("127.0.0.1", false);
        c1_connected = true;
        cse498::unique_buf buf;
        c1->register_mr(buf, FI_READ | FI_WRITE, 1);

        while (!mr_registered);

        *((uint64_t *) buf.get()) = 100;
        c1->write(buf, sizeof(uint64_t), 0, 2);
        mr2_wrote = true;

        while (!perms_updated);

        *((uint64_t *) buf.get()) = 1000;
        c1->write(buf, sizeof(uint64_t), 0, 2); // Make sure it can still write on 2

        c1_done = true;
        while (!c0_done);

        delete c1;
    });

    while (!c1_connected);
    // c2 stuff

    auto *c2 = new cse498::Connection("127.0.0.1", false);
    cse498::unique_buf buf;
    c2->register_mr(buf, FI_READ | FI_WRITE, 1);

    while (!mr_registered);
    *((uint64_t *) buf.get()) = 10;
    c2->write(buf, sizeof(uint64_t), 0, 1);
    mr1_wrote = true;

    while (!c1_done);

    c2->read(buf, sizeof(uint64_t), 0, 2); // Make sure it still has read access.
    ASSERT_EQ(1000, *((uint64_t *) buf.get()));

    *((uint64_t *) buf.get()) = 10000;
    c2->write(buf, sizeof(uint64_t), 0, 1); // Make sure it can still write on 1.

    *((uint64_t *) buf.get()) = 100000;

    // TODO this next line currently causes the program to exit which makes the test fail. (even though exiting means it passed)
    // Once we update wait_write to return on failure instead of exit we need to re-add this line (with the proper assertion)
    // (I've tried to use ASSERT_EXIT with this but it seems to hang indefinitely, likely since it calls fork which I doubt works well with networking)
    // c2->wait_write(buf, sizeof(uint64_t), 0, 2); // Make sure it can't write on 2

    c2_done = true;

    f.get();
    f2.get();

    delete c2;
}

