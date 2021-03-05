#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>

// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
// #include <spdlog/spdlog.h>

#include <cstring>
#include <chrono>
#include <thread>
// #include <cassert>

#include <iostream>
// This is pretty neat!
// From here: https://stackoverflow.com/a/14038590
#define safe_call(ans) callCheck((ans), __FILE__, __LINE__)
inline int callCheck(int err, const char *file, int line, bool abort=true) {
	if (err < 0) {
		std::cout << "Error: " << err << " " << fi_strerror(-err) << " " << file << ":" << line << std::endl;
		exit(0);
	}
	return err;
}

const char *port = "8080";


int main(int argc, char **argv) {
	fi_info *hints, *info;

	hints = fi_allocinfo();
	hints->ep_attr->type = FI_EP_MSG;
	hints->caps = FI_MSG;

	bool is_client = argc == 2;
	std::string dest_addr;

	if (is_client) {
		std::cout << "Initializing client" << std::endl;
		dest_addr = argv[1];
		std::cout << "Connecting to " << argv[1] << std::endl;
		safe_call(fi_getinfo(FI_VERSION(1, 6), argv[1], port, 0, hints, &info));
	} else {
		std::cout << "Initializing server" << std::endl;
		safe_call(fi_getinfo(FI_VERSION(1, 6), nullptr, port, FI_SOURCE, hints, &info));
	}

	// Fabric object. 
	fid_fabric *fab;
	safe_call(fi_fabric(info->fabric_attr, &fab, nullptr));

	if (is_client) {
		// Domain in the fabric
		fid_domain *domain;
		safe_call(fi_domain(fab, info, &domain, nullptr));
		// TODO create active endpoint stuff.
		fi_eq_attr eq_attr = {};
		eq_attr.size = 2; // Minimum size, likely not required. 
		eq_attr.wait_obj = FI_WAIT_UNSPEC;
		fid_eq *eq;
		std::cout << "Opening event queue" << std::endl;
		safe_call(fi_eq_open(fab, &eq_attr, &eq, nullptr));

		fid_ep *ep;
		std::cout << "Creating passive endpoint" << std::endl;
		safe_call(fi_endpoint(domain, info, &ep, nullptr));

		std::cout << "Binding eq to pep" << std::endl;
		safe_call(fi_ep_bind(ep, &eq->fid, 0));
		safe_call(fi_enable(ep));
		std::cout << "Sending connection request" << std::endl;
		safe_call(fi_connect(ep, info->dest_addr, nullptr, 0));
		char *buf = new char[512];
		std::cout << "Waiting for connection accept" << std::endl;
		// std::this_thread::sleep_for(std::chrono::milliseconds(10000));
		struct fi_eq_cm_entry entry;
		uint32_t event;
		// ssize_t out = safe_call(fi_eq_read(eq, &event, buf, 512, 0));
		// std::cout << "Read " << out << std::endl;
		int out_len = safe_call(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
		if (event != FI_CONNECTED) {
			std::cerr << "Wrong event" << std::endl;
			exit(1);
		}
		std::cout << "Connected" << std::endl;
	} else {
		// Server
		fi_eq_attr eq_attr = {};
		eq_attr.size = 2; // Minimum size, likely not required. 
		eq_attr.wait_obj = FI_WAIT_UNSPEC;
		fid_eq *eq;
		std::cout << "Opening event queue" << std::endl;
		safe_call(fi_eq_open(fab, &eq_attr, &eq, nullptr));

		fid_pep *pep;
		std::cout << "Creating passive endpoint" << std::endl;
		safe_call(fi_passive_ep(fab, info, &pep, nullptr));

		std::cout << "Binding eq to pep" << std::endl;
		safe_call(fi_pep_bind(pep, &eq->fid, 0));
		std::cout << "Transitioning pep to listening state" << std::endl;
		safe_call(fi_listen(pep));
		// char *buf = new char[512];
		std::cout << "Waiting for connection request" << std::endl;
		uint32_t event;
		struct fi_eq_cm_entry entry;

		// May want to check that the address is correct. 
		int rd = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
		if (rd != sizeof(entry)) {
			std::cerr << "Probably and error" << std::endl;
			exit(1);
		}

		if (event != FI_CONNREQ) {
			std::cerr << "Incorrect event type" << std::endl;
			exit(1);
		}
		// Domain in the fabric
		fid_domain *domain;
		safe_call(fi_domain(fab, entry.info, &domain, nullptr));
		
		std::cout << "Connection request received" << std::endl;

		// Now we can allocate an active endpoint. 
		fid_ep *ep;
		std::cout << "Creating active endpoint" << std::endl;
		// info->handle = &pep->fid;
		safe_call(fi_endpoint(domain, entry.info, &ep, nullptr));

		std::cout << "Binding eq to pep" << std::endl;
		safe_call(fi_ep_bind(ep, &eq->fid, 0));

		safe_call(fi_enable(ep));
		// std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		safe_call(fi_accept(ep, nullptr, 0));

		int addr_len = safe_call(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
		if (event != FI_CONNECTED) {
			std::cerr << "Not a connected event" << std::endl;
			exit(1);
		}
		std::cout << "Connected!" << std::endl;
	}
}