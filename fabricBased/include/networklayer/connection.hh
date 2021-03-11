#pragma once

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>

#include <kvcg_logging.h>

#include <cstring>

/**
 * Checks if the value is negative and if so prints the error, otherwise returns the value. 
 * Pretty cool!
 **/
#define SAFE_CALL(ans) callCheck((ans), __FILE__, __LINE__)

inline int callCheck(int err, const char *file, int line, bool abort = true) {
    if (err < 0) {
        LOG2<ERROR>() << "ERROR (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        exit(0);
    }
    return err;
}

static_assert(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 6, "We require libfabric 1.6");

namespace cse498 {
    /**
     * A basic wrapper around fabric connected communications. Can currently send and receive messages.
     **/
    class Connection {
    public:
        /**
         * Creates the passive side of a connection (it listens to a connection request from another machine using the other contructor)
         **/
        Connection() {
            create_hints();

            LOG2<DEBUG>() << "Initializing passive connection";
            SAFE_CALL(fi_getinfo(FI_VERSION(1, 6), nullptr, DEFAULT_PORT, FI_SOURCE, hints,
                                 &info)); // TODO I don't beleive FI_SOURCE does anything. Should try to delete.

            LOG2<TRACE>() << "Creating fabric";
            SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));

            open_eq();

            fid_pep *pep;
            LOG2<TRACE>() << "Creating passive endpoint";
            SAFE_CALL(fi_passive_ep(fab, info, &pep, nullptr));

            LOG2<TRACE>() << "Binding eq to pep";
            SAFE_CALL(fi_pep_bind(pep, &eq->fid, 0));
            LOG2<TRACE>() << "Transitioning pep to listening state";
            SAFE_CALL(fi_listen(pep));

            uint32_t event;
            struct fi_eq_cm_entry entry = {};
            LOG2<TRACE>() << "Waiting for connection request";
            int rd = SAFE_CALL(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
            // May want to check that the address is correct.
            if (rd != sizeof(entry)) {
                LOG2<ERROR>() << "There was an error reading the connection request.";
                exit(1);
            }

            if (event != FI_CONNREQ) {
                LOG2<ERROR>() << "Incorrect event type";
                exit(1);
            }
            fi_close(&pep->fid);
            fi_freeinfo(info);
            info = entry.info;
            LOG2<TRACE>() << "Connection request received";

            setup_active_ep();

            LOG2<TRACE>() << "Accepting connection request";
            SAFE_CALL(fi_accept(ep, nullptr, 0));

            wait_for_eq_connected();
        }

        /**
         * Creates the active side of a connection.
         * @param addr The address of the machine to connect to (that machine should have the passive side of a connection)
         **/
        Connection(const char *addr) {
            create_hints();

            LOG2<DEBUG>() << "Initializing client";
            SAFE_CALL(fi_getinfo(FI_VERSION(1, 6), addr, DEFAULT_PORT, 0, hints, &info));

            SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));

            open_eq();

            setup_active_ep();

            LOG2<TRACE>() << "Sending connection request";
            SAFE_CALL(fi_connect(ep, info->dest_addr, nullptr, 0));

            wait_for_eq_connected();
        }

        ~Connection() {
            LOG2<TRACE>() << "Closing all the fabric objects";
            fi_freeinfo(hints);
            fi_freeinfo(info);
            fi_close(&fab->fid);
            fi_close(&domain->fid);
            fi_close(&eq->fid);
            fi_close(&ep->fid);
            fi_close(&rx_cntr->fid);
            fi_close(&tx_cntr->fid);
        }

        /**
         * Sends a message through the endpoint, blocking until completion. This will
         * also block until all previous messages sent by async_send are completed as
         * well (this means you can touch any data buffer from any previous send after
         * calling this)
         *
         * @param data The data to send
         * @param size The size of the data
         **/
        inline void wait_send(const char *data, const size_t size) {
            async_send(data, size);
            wait_for_sends();
        }

        /**
         * This adds a message to the queue to be sent. It does not block. You cannot
         * touch the data buffer until after wait_for_sends or wait_send is called,
         * otherwise it may send the modified data buffer which is very bad (you should
         * call one of those also before the program completes otherwise messages from
         * async_send may not have been sent).
         *
         * @param data The data to send
         * @param size The size of the data
         **/
        inline void async_send(const char *data, const size_t size) {
            if (size > MAX_MSG_SIZE) {
                LOG2<ERROR>() << "Too large of a message!";
                exit(1);
            }
            ++msg_sends;
            SAFE_CALL(fi_send(ep, data, size, nullptr, 0, nullptr));
        }

        /**
         * Ensures all the previous sends were completed. This means after calling this
         * you can modify the data buffer from async_send.
         **/
        inline void wait_for_sends() {
            SAFE_CALL(wait_for_counter(tx_cntr, msg_sends));
        }

        /**
         * Blocks until it receives a message from the endpoint.
         *
         * @param buf The buffer to store the message data in
         * @param max_len The maximum length of the message (should be <= MAX_MSG_SIZE)
         **/
        inline void wait_recv(char *buf, size_t max_len) {
            uint64_t init = fi_cntr_read(rx_cntr);
            SAFE_CALL(fi_recv(ep, buf, max_len, nullptr, 0, nullptr));
            SAFE_CALL(wait_for_counter(rx_cntr, init + 1));
        }

    private:
        const char *DEFAULT_PORT = "8080";
        const size_t MAX_MSG_SIZE = 4096;
        uint64_t msg_sends = 0;

        // These need to be closed by fabric
        fi_info *hints, *info;
        fid_fabric *fab;
        fid_domain *domain;
        fid_eq *eq;
        fid_ep *ep;
        fid_cntr *tx_cntr, *rx_cntr;

        /**
         * Waits for the threshold to be reached on a counter.
         * @param cntr The counter to wait on.
         * @param threshold The value to wait to reach (>= threshold)
         * @return -1 on error, 0 otherwise (counters aren't great with reporting specific errors unfortunately)
         */
        inline int wait_for_counter(fid_cntr *cntr, uint64_t threshold) {
            uint64_t cntr_val;
            while (1) {
                cntr_val = SAFE_CALL(fi_cntr_read(cntr));
                if (cntr_val >= threshold) {
                    return 0;
                }
                if (SAFE_CALL(fi_cntr_readerr(cntr)) > 0) {
                    LOG2<ERROR>() << "There was an error on the counter";
                    return -1;
                }
            }
        }

        /**
         * Allocates hints, and sets the correct settings for the connection.
         *
         * Requires nothing to be set
         **/
        inline void create_hints() {
            hints = fi_allocinfo();
            hints->ep_attr->type = FI_EP_MSG;
            hints->caps = FI_MSG;
        }

        /**
         * Opens the event queue with the appropriate settings. No binding is performed.
         *
         * Requires fab to be set
         **/
        inline void open_eq() {
            fi_eq_attr eq_attr = {};
            eq_attr.size = 1; // Minimum size, maybe not required.
            eq_attr.wait_obj = FI_WAIT_UNSPEC;
            LOG2<TRACE>() << "Opening event queue";
            SAFE_CALL(fi_eq_open(fab, &eq_attr, &eq, nullptr));
        }

        /**
         * Sets up and binds the two counters (one for receiving messages, and another
         * for sending)
         *
         * Requires domain, ep to be set
         **/
        inline void setup_cntrs() {
            LOG2<TRACE>() << "Opening rx and tx counters";
            fi_cntr_attr cntr_attr = {};
            cntr_attr.events = FI_CNTR_EVENTS_COMP;
            cntr_attr.wait_obj = FI_WAIT_NONE;
            SAFE_CALL(fi_cntr_open(domain, &cntr_attr, &rx_cntr, nullptr));
            SAFE_CALL(fi_cntr_open(domain, &cntr_attr, &tx_cntr, nullptr));
            SAFE_CALL(fi_ep_bind(ep, &rx_cntr->fid, FI_RECV));
            SAFE_CALL(fi_ep_bind(ep, &tx_cntr->fid, FI_SEND));
        }

        /**
         * Performs a blocking read of the event queue until an FI_CONNECTED event is triggered.
         **/
        inline void wait_for_eq_connected() {
            struct fi_eq_cm_entry entry;
            uint32_t event;
            LOG2<TRACE>() << "Reading eq for FI_CONNECTED event";
            int addr_len = SAFE_CALL(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
            if (event != FI_CONNECTED) {
                LOG2<ERROR>() << "Not a connected event";
                exit(1);
            }
            LOG2<DEBUG>() << "Connected";
        }

        /**
         * Creates the domain, endpoint, counters, binds the event queue, and enables the ep.
         *
         * Requires fab, info to be set.
         **/
        inline void setup_active_ep() {
            LOG2<TRACE>() << "Creating domain";
            SAFE_CALL(fi_domain(fab, info, &domain, nullptr));

            LOG2<TRACE>() << "Creating active endpoint";
            SAFE_CALL(fi_endpoint(domain, info, &ep, nullptr));

            setup_cntrs();

            LOG2<TRACE>() << "Binding eq to pep";
            SAFE_CALL(fi_ep_bind(ep, &eq->fid, 0));

            LOG2<TRACE>() << "Enabling endpoint";
            SAFE_CALL(fi_enable(ep));
        }
    };
};