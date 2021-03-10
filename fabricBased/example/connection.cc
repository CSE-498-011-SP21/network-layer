#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "../include/connection.hh"

#include <iostream>
#include <chrono>
#include <thread>
/**
 * A very quick and dirty example changing permissions of a memory region. Can be run on two VMs. 
 * ./fabricBased/connection on one and two terminals each running ./fabricBased/connection <ip-addr> 
 * on the other VM. 
 **/
int main(int argc, char **argv) {
	spdlog::set_level(spdlog::level::trace);
	cse498::Connection *conn;
	if (argc == 2) {
		conn = new cse498::Connection(argv[1]);

		// Receive the response
		char *buf = new char[128];
		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);

		std::string msg = std::string(buf);
		conn->write(msg.c_str(), msg.length() + 1, 0);

		std::string msg0 = "wrote something\0";
		SPDLOG_INFO("Sending: {}", msg0);
		conn->wait_send(msg0.c_str(), msg0.length() + 1);

		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);

		msg = std::string(buf);
		conn->write(msg.c_str(), msg.length() + 1, 0);

		SPDLOG_INFO("Sending: {}", msg0);
		conn->wait_send(msg0.c_str(), msg0.length() + 1);
	} else {
		conn = new cse498::Connection();
		cse498::Connection *conn2 = new cse498::Connection();
		char *mr_buf = new char[128];
		// Registers the same memory regions on both connections. 
		conn->mr_reg(mr_buf, 128, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0);
		conn2->mr_reg(mr_buf, 128, FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0);
		std::string response = "write potato\0";
		SPDLOG_INFO("Sending: {}", response);
		conn->wait_send(response.c_str(), response.length() + 1);

		char *buf = new char[128];
		conn->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);
		SPDLOG_INFO("Now in buffer: {}", mr_buf);

		response = "write tomato\0";
		conn2->wait_send(response.c_str(), response.length() + 1);
		conn2->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);
		SPDLOG_INFO("Now in buffer: {}", mr_buf);

		conn->mr_reg(mr_buf, 128, FI_READ | FI_REMOTE_READ, 0);

		SPDLOG_INFO("Buffer still contains: {}", mr_buf);

		response = "write again\0";
		conn2->wait_send(response.c_str(), response.length() + 1);
		conn2->wait_recv(buf, 128);
		SPDLOG_INFO("Received: {}", buf);
		SPDLOG_INFO("Now in buffer: {}", mr_buf);

		response = "write once more\0";
		conn->wait_send(response.c_str(), response.length() + 1); // conn will write in response but not have permissions so it will fail. 

		std::chrono::milliseconds timespan(5000); // No response is sent, so wait_recv never completes. 
		std::this_thread::sleep_for(timespan);
		SPDLOG_INFO("Now in buffer: {}", mr_buf);
	}
	delete conn;
}