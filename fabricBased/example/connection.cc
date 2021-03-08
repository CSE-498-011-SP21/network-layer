#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "../include/connection.hh"

#include <iostream>

/**
 * A basic program to test and demonstrate connection.cc's functionality. The two machines will connect and each 
 * send a message to each other. The messages are sent in order (as opposed to simultaneously)
 **/
int main(int argc, char **argv) {
	spdlog::set_level(spdlog::level::trace);
	cse498::Connection *conn;
	if (argc == 2) {
		conn = new cse498::Connection(argv[1]);
		// Send initial message
		std::string msg = "potato\0";
		SPDLOG_INFO("Sending: {}", msg);
		conn->ssend(msg.c_str(), 7);
		// Receive the response
		char *buf = new char[128];
		conn->srecv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);
	} else {
		conn = new cse498::Connection();
		// Receive initial message
		char *buf = new char[128];
		conn->srecv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);
		// Send the response
		std::string response = "tomato\0";
		SPDLOG_INFO("Sending: {}", response);
		conn->ssend(response.c_str(), 7);
	}
	delete conn;
}