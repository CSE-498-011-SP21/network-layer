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

#include <iostream>
// This is pretty neat!
// From here: https://stackoverflow.com/a/14038590
#define SAFE_CALL(ans) callCheck((ans), __FILE__, __LINE__)
inline int callCheck(int err, const char *file, int line, bool abort=true) {
	if (err < 0) {
        SPDLOG_CRITICAL("ERROR ({0}): {1} {2}:{3}", err, fi_strerror(-err), file, line);
		exit(0);
	}
	return err;
}

static_assert(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 6, "We require libfabric 1.6");

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
			return ret;
		}
	}
}

/**
 * A basic wrapper around fabric connected communications. Can currently send and receive messages. 
 **/
class Connection {
	public:
		char *remote_buf = new char[MAX_MSG_SIZE];

		/**
		 * Creates the passive side of a connection (it listens to a connection request from another machine using the other contructor)
		 **/
		Connection() {
			create_hints();

			SPDLOG_DEBUG("Initializing passive connection");
			SAFE_CALL(fi_getinfo(FI_VERSION(1, 6), nullptr, DEFAULT_PORT, FI_SOURCE, hints, &info)); // TODO I don't beleive FI_SOURCE does anything. Should try to delete. 

			SPDLOG_TRACE("Creating fabric");
			SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));

			// Server
			open_eq();

			fid_pep *pep; // TODO need to close this in the constructor
			SPDLOG_TRACE("Creating passive endpoint");
			SAFE_CALL(fi_passive_ep(fab, info, &pep, nullptr));

			SPDLOG_TRACE("Binding eq to pep");
			SAFE_CALL(fi_pep_bind(pep, &eq->fid, 0));
			SPDLOG_TRACE("Transitioning pep to listening state");
			SAFE_CALL(fi_listen(pep));
			
			uint32_t event;
			struct fi_eq_cm_entry entry = {};
			SPDLOG_TRACE("Waiting for connection request");
			int rd = SAFE_CALL(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
			// May want to check that the address is correct. 
			if (rd != sizeof(entry)) {
				SPDLOG_CRITICAL("There was an error reading the connection request. ");
				exit(1);
			}

			if (event != FI_CONNREQ) {
				SPDLOG_CRITICAL("Incorrect event type");
				exit(1);
			}
			fi_freeinfo(info); // TODO might break some stuff. 
			info = entry.info;
			SPDLOG_TRACE("Connection request received");
			SAFE_CALL(fi_domain(fab, info, &domain, nullptr));
			
			SPDLOG_TRACE("Creating active endpoint");
			SAFE_CALL(fi_endpoint(domain, info, &ep, nullptr));

			// fid_cntr *ctr;
			// fi_cntr_attr cntr_attr = {};
			// cntr_attr.events = FI_CNTR_EVENTS_COMP;
			// cntr_attr.wait_obj = FI_WAIT_UNSPEC;
			// safe_call(fi_cntr_open(domain, &cntr_attr, &ctr, nullptr));
			// safe_call(fi_ep_bind(ep, &ctr->fid, FI_SEND | FI_RECV));

			setup_cqs();

			SPDLOG_TRACE("Binding eq to pep");
			SAFE_CALL(fi_ep_bind(ep, &eq->fid, 0));

			SPDLOG_TRACE("Enabling endpoint");
			SAFE_CALL(fi_enable(ep));

			SPDLOG_TRACE("Accepting connection request");
			SAFE_CALL(fi_accept(ep, nullptr, 0));

			wait_for_eq_connected();
		}

		/**
		 * Creates the active side of a connection. 
		 * @param addr The address of the machine to connect to (that machine should have the passive side of a connection)
		 **/
		Connection(const char *addr) {
			create_hints();

			SPDLOG_DEBUG("Initializing client");
			SAFE_CALL(fi_getinfo(FI_VERSION(1, 6), addr, DEFAULT_PORT, 0, hints, &info));

			SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));

			SAFE_CALL(fi_domain(fab, info, &domain, nullptr));
			
			open_eq();

			SPDLOG_TRACE("Creating endpoint");
			SAFE_CALL(fi_endpoint(domain, info, &ep, nullptr));

			setup_cqs();

			SPDLOG_TRACE("Binding eq to ep");
			SAFE_CALL(fi_ep_bind(ep, &eq->fid, 0));

			SPDLOG_TRACE("Enabling ep");
			SAFE_CALL(fi_enable(ep));

			SPDLOG_TRACE("Sending connection request");
			SAFE_CALL(fi_connect(ep, info->dest_addr, nullptr, 0));

			wait_for_eq_connected();
		}

		~Connection() {
			SPDLOG_TRACE("Closing all the fabric objects");
			fi_freeinfo(hints);
			fi_freeinfo(info);
			fi_close(&fab->fid);
			fi_close(&domain->fid);
			fi_close(&eq->fid);
			fi_close(&ep->fid);
			fi_close(&rx_cq->fid);
			fi_close(&tx_cq->fid);

			delete[] local_buf;
			delete[] remote_buf;
		}

		/**
		 * Sends a message through the endpoint, blocking until completion
		 * 
		 * @param data The data to send
		 * @param size The size of the data
		 **/
		void ssend(const char *data, const size_t size) {
			if (size > MAX_MSG_SIZE) {
				SPDLOG_CRITICAL("Too large of a message!");
				exit(1);
			}
			SAFE_CALL(fi_send(ep, data, size, nullptr, 0, nullptr));
			SAFE_CALL(wait_for_completion(tx_cq));
		}

		// I realized this will cause the next ssend after it to not wait which is 
		// bad (since there is already something in the tx_cq)
		// /**
		//  * Sends a message through the endpoint (no blocking). 
		//  * 
		//  * !IMPORTANT! If you are using this then you can't touch the data pointer until 
		//  * the message is sent (since there's no way of knowing when the message is sent
		//  * you effectively can never use the data pointer again.)
		//  * 
		//  * @param data The data to send
		//  * @param size The size of the data
		//  **/
		// void send(const char *data, const size_t size) {
		// 	if (size > MAX_MSG_SIZE) {
		// 		SPDLOG_CRITICAL("Too large of a message!");
		// 		exit(1);
		// 	}
		// 	SAFE_CALL(fi_send(ep, data, size, nullptr, 0, nullptr));
		// }

		/**
		 * Blocks until it receives a message from the endpoint. 
		 * 
		 * @param buf The buffer to store the message data in
		 * @param max_len The maximum length of the message (should be <= MAX_MSG_SIZE)
		 **/
		void srecv(char *buf, size_t max_len) {
			SAFE_CALL(fi_recv(ep, buf, max_len, nullptr, 0, nullptr));
			SAFE_CALL(wait_for_completion(rx_cq));
		}
	private:
		const char *DEFAULT_PORT = "8080";
		const size_t MAX_MSG_SIZE = 4096;
		char *local_buf = new char[MAX_MSG_SIZE];

		// These need to be closed by fabric
		fi_info *hints, *info;
		fid_fabric *fab;
		fid_domain *domain;
		fid_eq *eq;
		fid_ep *ep;
		fid_cq *rx_cq, *tx_cq; // TODO maybe replace these with a counter?

		/**
		 * Allocates hints, and sets the correct settings for the connection.
		 **/
		void create_hints() {
			hints = fi_allocinfo();
			hints->ep_attr->type = FI_EP_MSG;
			hints->caps = FI_MSG;
		}

		/**
		 * Opens the event queue with the appropriate settings. No binding is performed
		 **/
		void open_eq() {
			fi_eq_attr eq_attr = {};
			eq_attr.size = 1; // Minimum size, likely not required. 
			eq_attr.wait_obj = FI_WAIT_UNSPEC;
			SPDLOG_TRACE("Opening event queue");
			SAFE_CALL(fi_eq_open(fab, &eq_attr, &eq, nullptr));
		}

		/**
		 * Opens both the rx and tx cqs with the correct preferences, and binds them with the endpoint. 
		 */
		void setup_cqs() {
			SPDLOG_TRACE("Creating rx_cq and tx_cq");
			struct fi_cq_attr cq_attr = {};
			cq_attr.size = info->rx_attr->size;
			cq_attr.wait_obj = FI_WAIT_UNSPEC;
			SAFE_CALL(fi_cq_open(domain, &cq_attr, &rx_cq, nullptr));
			SAFE_CALL(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));
			cq_attr.size = info->tx_attr->size;
			SAFE_CALL(fi_cq_open(domain, &cq_attr, &tx_cq, nullptr));
			SAFE_CALL(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
		}

		/**
		 * Performs a blocking read of the event queue until an FI_CONNECTED event is triggered. 
		 **/
		void wait_for_eq_connected(){
			struct fi_eq_cm_entry entry;
			uint32_t event;
			SPDLOG_TRACE("Reading eq for FI_CONNECTED event");
			int addr_len = SAFE_CALL(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
			if (event != FI_CONNECTED) {
				SPDLOG_CRITICAL("Not a connected event");
				exit(1);
			}
			SPDLOG_DEBUG("Connected");
		}
};