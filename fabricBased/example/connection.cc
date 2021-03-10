#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "../include/connection.hh"

#include <iostream>

/**
 * A basic program to test and demonstrate connection.cc's functionality. The two machines will connect and send
 * a mix of async and blocking messages to each other. I dub this hamburger consensus. 
 **/
int main(int argc, char **argv) {
	spdlog::set_level(spdlog::level::trace);
	cse498::Connection *conn;
	if (argc == 2) {
		conn = new cse498::Connection(argv[1]);

		std::string msg0 = "Here are your burger ingredients\0";
		SPDLOG_INFO("Sending: {}", msg0);
		conn->wait_send(msg0.c_str(), msg0.length() + 1);

		std::string msg1 = "Bun\0";
		SPDLOG_INFO("Sending: {}", msg1);
		conn->async_send(msg1.c_str(), msg1.length() + 1);

		std::string msg2 = "Patty\0";
		SPDLOG_INFO("Sending: {}", msg2);
		conn->async_send(msg2.c_str(), msg2.length() + 1);

		std::string msg3 = "Cheese\0";
		SPDLOG_INFO("Sending: {}", msg3);
		conn->async_send(msg3.c_str(), msg3.length() + 1);

		conn->wait_for_sends();

		// Receive the response
		char *buf = new char[128];
		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);
	} else {
		conn = new cse498::Connection();
		// Receive initial message
		char *buf = new char[128];
		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);

		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);

		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);

		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);

		// Send the response
		std::string response = "Wow that looks like a delicious burger!\0";
		SPDLOG_INFO("Sending: {}", response);
		conn->async_send(response.c_str(), response.length() + 1);
		conn->wait_for_sends();
	}
	delete conn;
}