#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "connection.hh"

int main(int argc, char **argv) {
	spdlog::set_level(spdlog::level::trace);
	Connection *conn;
	if (argc == 2) {
		conn = new Connection(argv[1]);
		std::string msg = "potato\0";
		conn->ssend(msg.c_str(), 7);
	} else {
		conn = new Connection();
		char *buf = new char[128];
		conn->srecv(buf, 128);
		std::cout << buf << std::endl;
	}
	delete conn;
}