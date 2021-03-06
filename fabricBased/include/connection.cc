#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

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

int wait_for_completion(struct fid_cq *cq) {
	fi_cq_entry entry;
	int ret;
	while (1) {
		ret = fi_cq_read(cq, &entry, 1);
		if (ret > 0) return 0;
		if (ret != -FI_EAGAIN) {
			// New error on queue
			struct fi_cq_err_entry err_entry;
			fi_cq_readerr(cq, &err_entry, 0);
			SPDLOG_WARN("{0} {1}", fi_strerror(err_entry.err),
						fi_cq_strerror(cq, err_entry.prov_errno,
										err_entry.err_data, NULL, 0));
			std::cerr << "ERROR " << std::endl;
			return ret;
		}
	}
}

const char *port = "8080";
const size_t max_msg_size = 4096;
char *local_buf;
char *remote_buf;

int main(int argc, char **argv) {
	fid_mr *mr;
	remote_buf = new char[max_msg_size];
	local_buf = new char[max_msg_size];

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

		fid_cq *rq;
		struct fi_cq_attr cq_attr = {};
		cq_attr.size = info->rx_attr->size;
		cq_attr.wait_obj = FI_WAIT_UNSPEC;
		safe_call(fi_cq_open(domain, &cq_attr, &rq, nullptr));
		safe_call(fi_ep_bind(ep, &rq->fid, FI_RECV));
		fid_cq *tq;
		cq_attr.size = info->tx_attr->size;
		safe_call(fi_cq_open(domain, &cq_attr, &tq, nullptr));
		safe_call(fi_ep_bind(ep, &rq->fid, FI_TRANSMIT));

		// fid_cntr *ctr;
		// fi_cntr_attr cntr_attr = {};
		// cntr_attr.events = FI_CNTR_EVENTS_COMP;
		// cntr_attr.wait_obj = FI_WAIT_UNSPEC;
		// safe_call(fi_cntr_open(domain, &cntr_attr, &ctr, nullptr));
		// safe_call(fi_ep_bind(ep, &ctr->fid, FI_SEND | FI_RECV));

		std::cout << "Binding eq to pep" << std::endl;
		safe_call(fi_ep_bind(ep, &eq->fid, 0));
		safe_call(fi_enable(ep));
		std::cout << "Sending connection request" << std::endl;
		safe_call(fi_connect(ep, info->dest_addr, nullptr, 0));

		safe_call(fi_mr_reg(domain, remote_buf, max_msg_size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));

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

		safe_call(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
		safe_call(wait_for_completion(rq));
		// char *msg = new char[7];
		// fi_cntr_wait(ctr, 1, 2000);
		// std::cout << "Counter Vals " << fi_cntr_read(ctr) << " : " << fi_cntr_readerr(ctr) << std::endl;
		// std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		// struct fi_cq_entry cq_entry = {};
		// ssize_t size = safe_call(fi_cq_sread(rq, &cq_entry, 1, nullptr, -1));
		// int size = safe_call(fi_recv(ep, (void*) msg, 7, nullptr, 0, nullptr));
		std::cout << "Received " << remote_buf << std::endl;//<< msg[0] << msg[1] << msg[2] << msg[3] << std::endl;
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

		// fid_cntr *ctr;
		// fi_cntr_attr cntr_attr = {};
		// cntr_attr.events = FI_CNTR_EVENTS_COMP;
		// cntr_attr.wait_obj = FI_WAIT_UNSPEC;
		// safe_call(fi_cntr_open(domain, &cntr_attr, &ctr, nullptr));
		// safe_call(fi_ep_bind(ep, &ctr->fid, FI_SEND | FI_RECV));

		fid_cq *rq;
		struct fi_cq_attr cq_attr = {};
		cq_attr.size = info->rx_attr->size;
		cq_attr.wait_obj = FI_WAIT_UNSPEC;
		safe_call(fi_cq_open(domain, &cq_attr, &rq, nullptr));
		safe_call(fi_ep_bind(ep, &rq->fid, FI_RECV));
		fid_cq *tq;
		cq_attr.size = info->tx_attr->size;
		safe_call(fi_cq_open(domain, &cq_attr, &tq, nullptr));
		safe_call(fi_ep_bind(ep, &rq->fid, FI_TRANSMIT));

		std::cout << "Binding eq to pep" << std::endl;
		safe_call(fi_ep_bind(ep, &eq->fid, 0));

		safe_call(fi_enable(ep));

		safe_call(fi_mr_reg(domain, remote_buf, max_msg_size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));

		// std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		safe_call(fi_accept(ep, nullptr, 0));

		int addr_len = safe_call(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
		if (event != FI_CONNECTED) {
			std::cerr << "Not a connected event" << std::endl;
			exit(1);
		}
		std::string data = "potato";
		memcpy(local_buf, data.c_str(), data.length());
		std::cout << "Connected!" << std::endl;
		safe_call(fi_send(ep, local_buf, data.length(), nullptr, 0, nullptr));
		safe_call(wait_for_completion(tq));
		// std::string mes = "potato";
		// std::cout << "Counter value before send " << fi_cntr_read(ctr) << std::endl;
		// safe_call(fi_send(ep, (void*) mes.c_str(), 7, nullptr, 0, nullptr));
		// safe_call(fi_cntr_wait(ctr, 1, -1));
		// std::cout << "Sent " << std::endl;
		// std::this_thread::sleep_for(std::chrono::milliseconds(10000));
	}
}