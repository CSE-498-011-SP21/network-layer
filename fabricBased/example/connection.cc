#include <networklayer/connection.hh>
#include <iostream>
#include <chrono>
#include <thread>

int LOG_LEVEL = TRACE;

/**
 * A basic program to test and demonstrate connection.cc's functionality. The two machines will connect and send
 * a mix of async and blocking messages to each other. I dub this hamburger consensus. 
 **/
int main(int argc, char **argv) {
    int port = 8080;
    cse498::Connection *conn;
    if (argc == 2) {
        conn = new cse498::Connection(argv[1], port);
        conn->connect();

        cse498::unique_buf buf(128);
        conn->recv(buf, 128);
        LOG2<INFO>() << "Received: " << buf.get();
        // Wait write only writes when the message is sent right after. Maybe because it does something with the NIC data caching, but idk. The read never seems to work.
        std::string rma_msg = "Howdy!\0";
        buf = rma_msg;
        conn->write(buf, 7, 0, 0);
        LOG2<INFO>() << "RMA write complete";

        std::string msg3 = "wrote to mr\0";
        buf = msg3;
        LOG2<INFO>() << "Sending: " << msg3;
        conn->send(buf, msg3.length() + 1);

        // char *buf2 = new char[128];
        // conn->wait_read(buf2, 128, 0, 0);

        // buf = new char[128];
        // conn->wait_recv(buf, 128);
        // LOG2<INFO>() << "Received: " << buf;

        // std::chrono::milliseconds timespan(2000); // or whatever

        // std::this_thread::sleep_for(timespan);
        // LOG2<INFO>() << "RMA Buf contains: " << buf2;
    } else {
        const char *addr = "127.0.0.1";
        conn = new cse498::Connection(addr);
        conn->connect();
        cse498::unique_buf mr;
        std::string init_msg = "abc\0";
        mr = init_msg;
        uint64_t key = 0;
        conn->register_mr(mr, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, key);

        // Send the response
        std::string response = "The mr is registered\0";
        cse498::unique_buf buf;
        buf = response;
        LOG2<INFO>() << "Sending: " << response;
        conn->send(buf, response.length() + 1);

        conn->recv(buf, 128);
        LOG2<INFO>() << "Received: " << buf.get();

        // std::string msg3 = "Was the MR read now?\0";
        // LOG2<INFO>() << "Sending: " << msg3;
        // conn->wait_send(msg3.c_str(), msg3.length() + 1);
    }
    delete conn;
}