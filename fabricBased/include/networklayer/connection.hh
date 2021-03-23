#pragma once

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>

#include <kvcg_log2.hh>

#include <functional>
#include <cstring>
#include <map>

/**
 * Checks if the value is negative and if so prints the error, otherwise returns the value. 
 * Pretty cool!
 **/
#define SAFE_CALL(ans) callCheck((ans), __FILE__, __LINE__)

inline int callCheck(int err, const char *file, int line, bool abort = true) {
    if (err < 0) {
        DO_LOG(ERROR) << "ERROR (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        exit(1);
    }
    return err;
}

#define ERRREPORT2(x) error_report2((x), __FILE__, __LINE__);

inline bool error_report2(int err, std::string file, int line) {
    if (err) {
        LOG2<TRACE>() << "ERROR (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        return false;
    }
    return true;
}

#define MAJOR_VERSION_USED 1
#define MINOR_VERSION_USED 9

static_assert(FI_MAJOR_VERSION == MAJOR_VERSION_USED && FI_MINOR_VERSION >= MINOR_VERSION_USED,
              "Rely on libfabric 1.9");

#if FI_MINOR_VERSION > MINOR_VERSION_USED
#warning "We test on libfabric 1.9 and do not guarentee it will work if semantics are broken in later versions."
#endif

namespace cse498 {
    /**
     * A basic wrapper around fabric connected communications. Can currently send and receive messages.
     **/
    class Connection {
    public:
        /**
         * Creates one side of the connection (either client or server). Blocks until a connection is established. 
         * The address can be null if this is the server side and it is not connecting to 127.0.0.1 
         * 
         * If you are creating the server side of a connection connecting to 127.0.0.1 then you have to use this constructor. 
         * 
         * @param address address to use on the network, can be nullptr if the address is not 127.0.0.1 and it is the server
         * @param is_server Whether this machine is the server (doesn't matter which one in a connection is the server as long as one is)
         * @param port Port to connection on. Defaults to 8080
         **/
        Connection(const char *addr, bool is_server, const int port = 8080) {
            create_hints();

            if (is_server) {
                LOG2<DEBUG>() << "Initializing passive connection";
                std::string *server_addr = nullptr; // For localhost you have to define the address, but if its not localhost it will crash if the address is defined. libfabric is weird.
                if (addr != nullptr && strcmp(addr, "127.0.0.1") == 0) { // Lazy execution is neat
                    server_addr = new std::string("127.0.0.1");
                }
                SAFE_CALL(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED),
                                     server_addr == nullptr ? nullptr : server_addr->c_str(),
                                     std::to_string(port).c_str(), FI_SOURCE,
                                     hints, &info));
                LOG2<TRACE>() << "Creating fabric";
                SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));
                LOG2<DEBUG>() << "Using provider: " << info->fabric_attr->prov_name;

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
                info = entry.info;
                LOG2<TRACE>() << "Connection request received";

                setup_active_ep();

                LOG2<TRACE>() << "Accepting connection request";
                SAFE_CALL(fi_accept(ep, nullptr, 0));

                wait_for_eq_connected();
            } else {
                LOG2<DEBUG>() << "Initializing client";
                SAFE_CALL(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED), addr,
                                     std::to_string(port).c_str(), 0, hints,
                                     &info));
                LOG2<DEBUG>() << "Using provider: " << info->fabric_attr->prov_name;

                SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));

                open_eq();

                setup_active_ep();

                LOG2<TRACE>() << "Sending connection request";
                while (fi_connect(ep, info->dest_addr, nullptr, 0) < 0);
                LOG2<TRACE>() << "Connection request sent";

                wait_for_eq_connected();
            }
        }

        /**
         * Creates a server side of the connection. Blocks until completion. 
         * Cannot be used for local connections (there is another constructor for that)
         * 
         * @param port the port to connect on. Defaults to 8080
         **/
        Connection(const int port = 8080) : Connection(nullptr, true, port) {}

        /**
         * Creates the client side of the connection, blocking until completion. If there is no
         * server active while the constructor is called it will continue to try to connect until
         * the server is found.
         * 
         * @param addr the address of the server
         * @param port the port to connect on (default 8080)
         **/
        Connection(const char *addr, const int port = 8080) : Connection(addr, false, port) {}

        Connection(const Connection &) = delete;

        Connection(Connection &&other) {
            msg_sends = other.msg_sends;

            // These need to be closed by fabric
            hints = other.hints;
            other.hints = nullptr;
            info = other.info;
            other.info = nullptr;
            fab = other.fab;
            other.fab = nullptr;

            domain = other.domain;
            other.domain = nullptr;
            eq = other.eq;
            other.eq = nullptr;
            ep = other.ep;
            other.ep = nullptr;
            rx_cq = other.rx_cq;
            other.rx_cq = nullptr;
            tx_cq = other.tx_cq;
            other.tx_cq = nullptr;
            mrs = other.mrs;
            other.mrs = nullptr;

        }

        ~Connection() {
            LOG2<TRACE>() << "Closing all the fabric objects";
            fi_freeinfo(hints);
            fi_freeinfo(info);
            if (fab) {
                fi_close(&fab->fid);
                fi_close(&domain->fid);
                fi_close(&eq->fid);
                fi_close(&ep->fid);
                fi_close(&rx_cq->fid);
                fi_close(&tx_cq->fid);
                for (auto it = mrs->begin(); it != mrs->end(); ++it) {
                    fi_close(&it->second->fid);
                }
            }
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
            LOG2<DEBUG3>() << "Sending " << size << " bytes";
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
            while (msg_sends > 0) {
                LOG2<DEBUG3>() << "Waiting for " << msg_sends << " message(s) to send.";
                msg_sends -= SAFE_CALL(wait_for_completion(tx_cq));
            }
        }

        /**
         * Blocks until it receives a message from the endpoint.
         *
         * @param buf The buffer to store the message data in
         * @param max_len The maximum length of the message (should be <= MAX_MSG_SIZE)
         **/
        inline void wait_recv(char *buf, size_t max_len) {
            SAFE_CALL(fi_recv(ep, buf, max_len, nullptr, 0, nullptr));
            LOG2<DEBUG3>() << "Receiving up to " << max_len << " bytes";
            SAFE_CALL(wait_for_completion(rx_cq));
        }

        /**
         * Same as above but nonblocking.
         *
         * @param buf The buffer to store the message data in
         * @param max_len The maximum length of the message (should be <= MAX_MSG_SIZE)
         **/
        inline bool try_recv(char *buf, size_t max_len) {
            LOG2<DEBUG3>() << "Receiving up to " << max_len << " bytes";
            bool b = ERRREPORT2(fi_recv(ep, buf, max_len, nullptr, 0, nullptr));
            if (b) {
                SAFE_CALL(wait_for_completion(rx_cq));
                return true;
            }
            return false;
        }

        /**
         * Registers a new memory region which only works for this connection. If there is already
         * a memory region registered with the same key then it will close the previous one and register 
         * this one. You can use this to change permissions for a memory region on a specific connection 
         * (by calling this function again on the same buf, but with the new access flags)
         * 
         * @param buf The buffer to register
         * @param size The size of the buffer
         * @param access The access flags for the memory region (FI_WRITE, FI_REMOTE_WRITE, FI_READ, and/or FI_REMOTE_READ. Bitwise or for multiple permissions)
         * @param key The access key for the memory region. The other side of the connection should use the same key (0 works well for this)
         * @return True if there was another region with the same key that needed to be closed. 
         **/
        inline bool register_mr(char *buf, size_t size, uint64_t access, uint64_t key) {
            auto elem = mrs->find(key);
            if (elem == mrs->end()) {
                mrs->insert({key, create_mr(buf, size, access, key)});
                return false;
            } else {
                LOG2<INFO>() << "Closing old memory region";
                SAFE_CALL(fi_close(&elem->second->fid));
                elem->second = create_mr(buf, size, access, key);
                return true;
            }
        }

        /**
         * Write from buf with given size to the addr with the given key
         * Note addresses start at 0
         * @param buf
         * @param size
         * @param addr
         * @param key
         */
        inline void wait_write(const char *buf, size_t size, uint64_t addr, uint64_t key) {
            SAFE_CALL(fi_write(ep, buf, size, nullptr, 0, addr, key, nullptr));
            LOG2<DEBUG3>() << "Write " << key << "-" << addr << " sent";
            SAFE_CALL(wait_for_completion(tx_cq));
        }

        /**
         * Read size bytes from the addr with the given key into buf
         * @param buf
         * @param size
         * @param addr
         * @param key
         */
        inline void wait_read(char *buf, size_t size, uint64_t addr, uint64_t key) {
            SAFE_CALL(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            LOG2<DEBUG3>() << "Read " << key << "-" << addr << " sent";
            SAFE_CALL(wait_for_completion(tx_cq));
        }

        /**
         * Same as above but nonblocking
         * @param buf
         * @param size
         * @param addr
         * @param key
         */
        inline bool try_read(char *buf, size_t size, uint64_t addr, uint64_t key) {
            //SAFE_CALL(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            LOG2<DEBUG3>() << "Read " << key << "-" << addr << " sent";
            bool b = ERRREPORT2(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            if (b) {
                SAFE_CALL(wait_for_completion(tx_cq));
                return true;
            }
            return false;
        }

        /*inline void read_local(char *buf, size_t size, uint64_t addr, uint64_t key) {
            int ret = SAFE_CALL(fi_read(ep, buf, size, fi_mr_desc(mr), ~0, addr, key, nullptr));
            LOG2<INFO>() << "Read sent: " << ret;
            SAFE_CALL(wait_for_completion(tx_cq));
        }*/

    private:
        const size_t MAX_MSG_SIZE = 4096;
        uint64_t msg_sends = 0;

        // These need to be closed by fabric
        fi_info *hints, *info;
        fid_fabric *fab;
        fid_domain *domain;
        fid_eq *eq;
        fid_ep *ep;
        fid_cq *rx_cq, *tx_cq;
        std::map<uint64_t, fid_mr *> *mrs = new std::map<uint64_t, fid_mr *>();

        // Based on connectionless.hh, but not identical. This returns the value from fi_cq_read. 
        inline int wait_for_completion(struct fid_cq *cq) {
            fi_cq_msg_entry entry;
            int ret;
            while (1) {
                ret = fi_cq_read(cq, &entry,
                                 1); // TODO an rma write will likely cause the rx_cq to receive something, so I have to be careful about that.
                if (ret > 0) {
                    LOG2<TRACE>() << "Entry flags " << entry.flags;
                    LOG2<TRACE>() << "Entry rma " << (entry.flags & FI_RMA);
                    LOG2<TRACE>() << "Entry len " << entry.len;
                    LOG2<TRACE>() << "Entry ops " << entry.op_context;
                    return ret;
                }
                if (ret != -FI_EAGAIN) {
                    // New error on queue
                    struct fi_cq_err_entry err_entry;
                    fi_cq_readerr(cq, &err_entry, 0);
                    LOG2<ERROR>() << fi_cq_strerror(cq, err_entry.prov_errno, err_entry.err_data, nullptr, 0);
                    return ret;
                }
            }
        }

        inline fid_mr *create_mr(char *buf, size_t size, uint64_t access, uint64_t key) {
            fid_mr *mr;
            LOG2<TRACE>() << "Registering memory region";
            SAFE_CALL(fi_mr_reg(domain, buf, size, access, 0, key, 0, &mr, nullptr));
            LOG2<INFO>() << "MR KEY:" << fi_mr_key(mr);
            return mr;
        }

        /**
         * Allocates hints, and sets the correct settings for the connection.
         *
         * Requires nothing to be set
         **/
        inline void create_hints() {
            hints = fi_allocinfo();
            hints->caps = FI_MSG | FI_RMA | FI_ATOMIC;
            hints->ep_attr->type = FI_EP_MSG;
            //hints->ep_attr->protocol = FI_PROTO_SOCK_TCP;
            hints->domain_attr->mr_mode = FI_MR_SCALABLE;
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
         * Sets up and binds the rx and cq completion queues. 
         * 
         * Requires domain, info, ep to be set. 
         **/
        inline void setup_cqs() {
            fi_cq_attr cq_attr = {};
            cq_attr.wait_obj = FI_WAIT_NONE;
            cq_attr.size = info->tx_attr->size;
            cq_attr.format = FI_CQ_FORMAT_MSG;
            LOG2<TRACE>() << "Creating tx completion queue";
            SAFE_CALL(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            cq_attr.size = info->rx_attr->size;
            LOG2<TRACE>() << "Creating rx completion queue";
            SAFE_CALL(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));
            LOG2<TRACE>() << "Binding TX CQ to EP";
            SAFE_CALL(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
            LOG2<TRACE>() << "Binding RX CQ to EP";
            SAFE_CALL(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));
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

            setup_cqs();

            LOG2<TRACE>() << "Binding eq to pep";
            SAFE_CALL(fi_ep_bind(ep, &eq->fid, 0));

            LOG2<TRACE>() << "Enabling endpoint";
            SAFE_CALL(fi_enable(ep));
        }
    };

    /**
 * Performs best effort broadcast
 * @param clients clients to send to
 * @param message message to send
 * @param messageSize size of message
 */
    inline void bestEffortBroadcast(std::vector<Connection> &connections, const char *message, size_t messageSize) {
        for (auto &c : connections) {
            c.wait_send(message, messageSize);
        }
    }

    /**
     * Performs best effort broadcast recieve from client
     * @param clients clients to recv from
     * @param buf buffer
     * @param sizeOfBuf buffer size
     */
    inline void bestEffortBroadcastReceiveFrom(Connection &connections, char *buf, size_t sizeOfBuf) {
        connections.wait_recv(buf, sizeOfBuf);
    }

    /**
     * Reliably broadcast from a server
     * @param clients clients to send to
     * @param message message to send
     * @param messageSize size of message
     */
    inline void reliableBroadcast(std::vector<Connection> &connections, const char *message, size_t messageSize) {
        bestEffortBroadcast(connections, message, messageSize);
    }

    /**
     * Receive from
     * @param receiveFrom node to receive from
     * @param clients clients to send to
     * @param buf buffer to use (registered)
     * @param bufSize size of buffer to use
     * @param checkIfReceivedBefore function to check if the message has been received before
     * @param markAsReceived function to mark a message as received
     * @return true if it has not been received before
     */
    inline bool
    reliableBroadcastReceiveFrom(Connection &receiveFrom, std::vector<Connection> &connections,
                                 char *buf,
                                 size_t bufSize, const std::function<bool(char *, size_t)> &checkIfReceivedBefore,
                                 const std::function<void(char *, size_t)> &markAsReceived) {

        receiveFrom.wait_recv(buf, bufSize);

        if (!checkIfReceivedBefore(buf, bufSize)) {
            bestEffortBroadcast(connections, buf, bufSize);
            markAsReceived(buf, bufSize);
            return true;
        }
        return false;

    }
};