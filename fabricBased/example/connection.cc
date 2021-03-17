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
	cse498::Connection *conn;
	if (argc == 2) {
		conn = new cse498::Connection(argv[1]);

		char *buf = new char[128];
		conn->wait_recv(buf, 128);
		LOG2<INFO>() << "Received: " << buf;
		// Wait write only writes when the message is sent right after. Maybe because it does something with the NIC data caching, but idk. The read never seems to work.
		std::string rma_msg = "Howdy!\0";
		conn->wait_write(rma_msg.c_str(), rma_msg.length() + 1, 0, 0);
		LOG2<INFO>() << "RMA write complete";

		std::string msg3 = "wrote to mr\0";
		LOG2<INFO>() << "Sending: " << msg3;
		conn->wait_send(msg3.c_str(), msg3.length() + 1);

		// char *buf2 = new char[128];
		// conn->wait_read(buf2, 128, 0, 0);

		// buf = new char[128];
		// conn->wait_recv(buf, 128);
		// LOG2<INFO>() << "Received: " << buf;

		// std::chrono::milliseconds timespan(2000); // or whatever

		// std::this_thread::sleep_for(timespan);
		// LOG2<INFO>() << "RMA Buf contains: " << buf2;
	} else {
		conn = new cse498::Connection();
		char *mr = new char[256];
		std::string init_msg = "abc\0";
		memcpy(mr, init_msg.c_str(), init_msg.length() + 1);
		conn->register_mr(mr, 256, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0);

		// Send the response
		std::string response = "The mr is registered\0";
		LOG2<INFO>() << "Sending: " << response;
		conn->wait_send(response.c_str(), response.length() + 1);

		char *buf = new char[128];
		conn->wait_recv(buf, 128);
		LOG2<INFO>() << "Received: " << buf;

		// std::string msg3 = "Was the MR read now?\0";
		// LOG2<INFO>() << "Sending: " << msg3;
		// conn->wait_send(msg3.c_str(), msg3.length() + 1);
	}
	delete conn;
}