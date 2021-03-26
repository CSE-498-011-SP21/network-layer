#pragma once

#include "unique_buf.hh"
#include "shared_buf.hh"
#include "Macros.hh"

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <functional>
#include <cstring>
#include <map>
#include <cassert>

static_assert(FI_MAJOR_VERSION == MAJOR_VERSION_USED && FI_MINOR_VERSION >= MINOR_VERSION_USED,
              "Rely on libfabric 1.9");

#if FI_MINOR_VERSION > MINOR_VERSION_USED
#warning "We test on libfabric 1.9 and do not guarentee it will work if semantics are broken in later versions."
#endif

namespace cse498 {

    enum ProviderType {
        Verbs,
        Sockets
    };

    inline uint32_t providerToProtocol(ProviderType provider) {
        switch (provider) {
            case Verbs:
                return FI_PROTO_RDMA_CM_IB_RC;
            case Sockets:
                return FI_PROTO_SOCK_TCP;
        }
    }

    /**
     * A basic wrapper around fabric connected communications. Can currently send and receive messages.
     **/
    class Connection {
    public:
        /**
         * Creates one side of the connection (either client or server). Must call connect to complete the connection
         * The address can be null if this is the server side and it is not connecting to 127.0.0.1 
         * 
         * If you are creating the server side of a connection connecting to 127.0.0.1 then you have to use this constructor. 
         * 
         * @param address address to use on the network, can be nullptr if the address is not 127.0.0.1 and it is the server
         * @param is_server Whether this machine is the server (doesn't matter which one in a connection is the server as long as one is)
         * @param port Port to connection on. Defaults to 8080
         **/
        Connection(const char *addr, bool is_server, const int port = 8080, ProviderType provider = Sockets) {
            hints = nullptr;
            info = nullptr;
            fab = nullptr;
            domain = nullptr;
            eq = nullptr;
            ep = nullptr;
            rx_cq = nullptr;
            tx_cq = nullptr;
            pep = nullptr;
            this->is_server = is_server;

            create_hints(providerToProtocol(provider));

            if (is_server) {
                DO_LOG(DEBUG) << "Initializing passive connection";
                SAFE_CALL(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED),
                                     addr,
                                     std::to_string(port).c_str(), FI_SOURCE,
                                     hints, &info));
                DO_LOG(TRACE) << "Creating fabric";
                SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));
                DO_LOG(DEBUG) << "Using provider: " << info->fabric_attr->prov_name;

                open_eq();
            } else {
                DO_LOG(DEBUG) << "Initializing client";
                SAFE_CALL(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED), addr,
                                     std::to_string(port).c_str(), 0, hints,
                                     &info));
                DO_LOG(DEBUG) << "Using provider: " << info->fabric_attr->prov_name;

                SAFE_CALL(fi_fabric(info->fabric_attr, &fab, nullptr));

                open_eq();

                setup_active_ep();
            }
        }

        /**
         * Creates a server side of the connection. Must call connect to complete the connection.
         * Cannot be used for local connections (there is another constructor for that)
         * 
         * @param port the port to connect on. Defaults to 8080
         **/
        Connection(const int port = 8080) : Connection(nullptr, true, port) {}


        /**
         * Creates the client side of the connection. Must call connect to complete the connection.
         *
         * @param addr the address of the server
         * @param port the port to connect on (default 8080)
         **/
        Connection(const char *addr, const int port = 8080) : Connection(addr, false, port) {}

        Connection(const Connection &) = delete;

        Connection(Connection &&other) {
            msg_sends = other.msg_sends;
            is_server = other.is_server;

            // These need to be closed by fabric
            hints = other.hints;
            other.hints = nullptr;
            info = other.info;
            other.info = nullptr;
            fab = other.fab;
            other.fab = nullptr;
            pep = other.pep;
            other.pep = nullptr;

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
            DO_LOG(TRACE) << "Closing all the fabric objects";
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
         * Initializes the connection with the other side of the connection, blocking until completion.
         * This must be called by both the client and server.
         *
         * @return true on success
         */
        inline bool connect() {
            if (is_server) {

                createPep();

                uint32_t event = 0;
                struct fi_eq_cm_entry entry = {};
                DO_LOG(TRACE) << "Waiting for connection request";
                bool ret = ERRREPORT(fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0));
                // May want to check that the address is correct.
                if (!ret) {
                    DO_LOG(ERROR) << "There was an error reading the connection request.";
                    ERRREPORT(fi_close(&pep->fid));
                    return false;
                }

                if (event != FI_CONNREQ) {
                    DO_LOG(ERROR) << "Incorrect event type";
                    ERRREPORT(fi_close(&pep->fid));
                    return false;
                }

                info = entry.info;
                DO_LOG(TRACE) << "Connection request received";

                if (!try_setup_active_ep()) {
                    ERRREPORT(fi_close(&pep->fid));
                    return false;
                }

                ERRREPORT(fi_close(&pep->fid));
                DO_LOG(TRACE) << "Accepting connection request";
                ret = ERRREPORT(fi_accept(ep, nullptr, 0));
                if (!ret) {
                    return false;
                }

                return wait_for_eq_connected();
            } else {
                DO_LOG(TRACE) << "Sending connection request";
                if (fi_connect(ep, info->dest_addr, nullptr, 0) < 0) {
                    return false;
                }
                DO_LOG(TRACE) << "Connection request sent";

                return wait_for_eq_connected();
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
         * @param offset offset into buffer
         **/
        [[deprecated("Use with unique_buf instead")]]
        inline void send(const char *buf, size_t size) {
            async_send(buf, size);
            DO_LOG(DEBUG3) << "Sending " << size << " bytes";
            wait_for_sends();
        }

        /**
         * Sends a message through the endpoint, blocking until completion. This will
         * also block until all previous messages sent by async_send are completed as
         * well (this means you can touch any data buffer from any previous send after
         * calling this)
         *
         * @param data The data to send
         * @param size The size of the data
         * @param offset offset into buffer
         **/
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline void send(buf_t &data, size_t size, size_t offset = 0) {
            async_send(data, size, offset);
            DO_LOG(DEBUG3) << "Sending " << size << " bytes";
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
         * @return true on success
         **/
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline bool async_send(buf_t &data, const size_t size, size_t offset = 0) {
            assert(data.isRegistered());
            if (size + offset > MAX_MSG_SIZE) {
                DO_LOG(ERROR) << "Too large of a message!";
                exit(1);
            }
            ++msg_sends;
            return ERRREPORT(fi_send(ep, data.get() + offset, size, data.getDesc(), 0, nullptr));
        }

        [[deprecated("Use with unique_buf instead")]]
        inline bool async_send(const char *buf, const size_t size) {
            if (size > MAX_MSG_SIZE) {
                DO_LOG(ERROR) << "Too large of a message!";
                exit(1);
            }
            ++msg_sends;
            return ERRREPORT(fi_send(ep, buf, size, nullptr, 0, nullptr));
        }

        /**
         * This adds a message to the queue to be sent, blocking until completion. 
         *
         * @param data The data to send
         * @param size The size of the data
         * 
         * @return true on success
         **/
        inline bool try_send(const char *data, const size_t size) {
            if (size > MAX_MSG_SIZE) {
                LOG2<ERROR>() << "Too large of a message!";
                exit(1); // Exit for now to avoid possible infinite loops
            }

            ++msg_sends;
            bool b = ERRREPORT(fi_send(ep, data, size, nullptr, 0, nullptr));
            if (b) {
                if (try_wait_for_sends()) {
                    LOG2<TRACE>() << "Message sent";
                    return true;
                }
                return false;
            }
            LOG2<TRACE>() << "Message send failed";
            return false;
        }

        /**
         * Ensures all the previous sends were completed. This means after calling this
         * you can modify the data buffer from async_send.
         * 
         * @return true on success
         **/
        inline bool try_wait_for_sends() {
            while (msg_sends > 0) {
                LOG2<DEBUG3>() << "Waiting for " << msg_sends << " message(s) to send.";
                int ret = ERRREPORT(wait_for_completion(tx_cq));
                if (ret >= 0) {
                    msg_sends -= ret;
                } else {
                    return false;
                }
            }
            return true;
        }

        /**
         * Ensures all the previous sends were completed. This means after calling this
         * you can modify the data buffer from async_send.
         **/
        inline void wait_for_sends() {
            while (msg_sends > 0) {
                DO_LOG(DEBUG3) << "Waiting for " << msg_sends << " message(s) to send.";
                msg_sends -= SAFE_CALL(wait_for_completion(tx_cq));
            }
        }

        /**
         * Blocks until it receives a message from the endpoint.
         *
         * @param buf The buffer to store the message data in
         * @param max_len The maximum length of the message (should be <= MAX_MSG_SIZE)
         **/
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline void recv(buf_t &data, size_t max_len, size_t offset = 0) {
            assert(data.isRegistered());
            char *buf = data.get() + offset;
            SAFE_CALL(fi_recv(ep, buf, max_len, data.getDesc(), 0, nullptr));
            DO_LOG(DEBUG3) << "Receiving up to " << max_len << " bytes";
            SAFE_CALL(wait_for_completion(rx_cq));
        }

        [[deprecated("Use with unique_buf instead")]]
        inline void recv(char *buf, size_t max_len) {
            SAFE_CALL(fi_recv(ep, buf, max_len, nullptr, 0, nullptr));
            DO_LOG(DEBUG3) << "Receiving up to " << max_len << " bytes";
            SAFE_CALL(wait_for_completion(rx_cq));
        }

        /**
         * Blocks until it receives a message from the endpoint.
         *
         * @param buf The buffer to store the message data in
         * @param max_len The maximum length of the message (should be <= MAX_MSG_SIZE)
         * 
         * @return true on success
         **/
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline bool try_recv(buf_t &data, size_t max_len, size_t offset = 0) {
            assert(data.isRegistered());

            DO_LOG(DEBUG3) << "Receiving up to " << max_len << " bytes";
            bool b = ERRREPORT(fi_recv(ep, data.get() + offset, max_len, data.getDesc(), 0, nullptr));
            if (b) {
                return ERRREPORT(wait_for_completion(rx_cq));
            }
            return false;
        }

        [[deprecated("Use with unique_buf instead")]]
        inline bool try_recv(char *buf, size_t max_len) {
            DO_LOG(DEBUG3) << "Receiving up to " << max_len << " bytes";
            bool b = ERRREPORT(fi_recv(ep, buf, max_len, nullptr, 0, nullptr));
            if (b) {
                return ERRREPORT(wait_for_completion(rx_cq));
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
         * @param key The access key for the memory region. The other side of the connection should use the same key (0 works well for this). If the fabric changes it, it will set key.
         * @return True if there was another region with the same key that needed to be closed. 
         **/
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline bool register_mr(buf_t &data, uint64_t access, uint64_t &key) {
            auto elem = mrs->find(key);
            if (elem == mrs->end()) {
                fid_mr *mr = create_mr(data.get(), data.size(), access, key);
                mrs->insert({key, mr});
                data.registerMemoryCallback(key, fi_mr_desc(mr));
                return false;
            } else {
                DO_LOG(INFO) << "Closing old memory region";
                SAFE_CALL(fi_close(&elem->second->fid));
                auto key_before = key;
                elem->second = create_mr(data.get(), data.size(), access, key);
                assert(key_before == key);
                data.registerMemoryCallback(key, fi_mr_desc(elem->second));
                return true;
            }
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
        [[deprecated("Use with unique_buf instead")]]
        inline bool register_mr(char *buf, size_t size, uint64_t access, uint64_t &key) {
            auto elem = mrs->find(key);
            if (elem == mrs->end()) {
                auto mr = create_mr(buf, size, access, key);
                mrs->insert({key, mr});
                return false;
            } else {
                DO_LOG(INFO) << "Closing old memory region";
                SAFE_CALL(fi_close(&elem->second->fid));
                auto key_before = key;
                elem->second = create_mr(buf, size, access, key);
                assert(key_before == key);
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
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline void write(buf_t &data, size_t size, uint64_t addr, uint64_t key, size_t offset = 0) {
            assert(data.isRegistered());

            SAFE_CALL(fi_write(ep, data.get() + offset, size, data.getDesc(), 0, addr, key, nullptr));
            DO_LOG(DEBUG3) << "Write " << key << "-" << addr << " sent";
            SAFE_CALL(wait_for_completion(tx_cq));
        }

        /**
         * Write from buf with given size to the addr with the given key
         * Note addresses start at 0
         * @param buf
         * @param size
         * @param addr
         * @param key
         */
        [[deprecated("Use with unique_buf instead")]]
        inline void write(const char *buf, size_t size, uint64_t addr, uint64_t key) {
            SAFE_CALL(fi_write(ep, buf, size, nullptr, 0, addr, key, nullptr));
            DO_LOG(DEBUG3) << "Write " << key << "-" << addr << " sent";
            SAFE_CALL(wait_for_completion(tx_cq));
        }

        /**
         * Write from buf with given size to the addr with the given key. Blocks
         * until completion. 
         * Note addresses start at 0
         * @param data
         * @param size
         * @param addr
         * @param key
         * @param offset
         * 
         * @return true on success
         */
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline bool try_write(buf_t &data, size_t size, uint64_t addr, uint64_t key, size_t offset = 0) {
            assert(data.isRegistered());

            auto b = ERRREPORT(fi_write(ep, data.get() + offset, size, data.getDesc(), 0, addr, key, nullptr));
            if (b) {
                DO_LOG(DEBUG3) << "Write " << key << "-" << addr << " sent";
                return ERRREPORT(wait_for_completion(tx_cq));
            }
            return false;
        }

        [[deprecated("Use with unique_buf instead")]]
        inline bool try_write(const char *buf, size_t size, uint64_t addr, uint64_t key) {
            bool b = ERRREPORT(fi_write(ep, buf, size, nullptr, 0, addr, key, nullptr));
            LOG2<DEBUG3>() << "Write " << key << "-" << addr << " sent";
            if (b) {
                return ERRREPORT(wait_for_completion(tx_cq));
            }
            return false;
        }

        /**
         * Read size bytes from the addr with the given key into buf. 
         * @param buf
         * @param size
         * @param addr
         * @param key
         */
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline void read(buf_t &data, size_t size, uint64_t addr, uint64_t key, size_t offset = 0) {
            assert(data.isRegistered());

            SAFE_CALL(fi_read(ep, data.get() + offset, size, data.getDesc(), 0, addr, key, nullptr));
            DO_LOG(DEBUG3) << "Read " << key << "-" << addr << " sent";
            SAFE_CALL(wait_for_completion(tx_cq));
        }

        [[deprecated("Use with unique_buf instead")]]
        inline void read(char *buf, size_t size, uint64_t addr, uint64_t key) {
            SAFE_CALL(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            DO_LOG(DEBUG3) << "Read " << key << "-" << addr << " sent";
            SAFE_CALL(wait_for_completion(tx_cq));
        }


        /**
         * Read size bytes from the addr with the given key into buf. Blocks
         * until completion. 
         * @param buf
         * @param size
         * @param addr
         * @param key
         * 
         * @return true on success
         */
        template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
        inline bool try_read(buf_t &data, size_t size, uint64_t addr, uint64_t key, size_t offset = 0) {
            assert(data.isRegistered());

            //SAFE_CALL(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            DO_LOG(DEBUG3) << "Read " << key << "-" << addr << " sent";
            bool b = ERRREPORT(fi_read(ep, data.get() + offset, size, data.getDesc(), 0, addr, key, nullptr));
            if (b) {
                return ERRREPORT(wait_for_completion(tx_cq));
            }
            return false;
        }

        [[deprecated("Use with unique_buf instead")]]
        inline bool try_read(char *buf, size_t size, uint64_t addr, uint64_t key, size_t offset = 0) {
            //SAFE_CALL(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            DO_LOG(DEBUG3) << "Read " << key << "-" << addr << " sent";
            bool b = ERRREPORT(fi_read(ep, buf, size, nullptr, 0, addr, key, nullptr));
            if (b) {
                return ERRREPORT(wait_for_completion(tx_cq));
            }
            return false;
        }

        /*inline void read_local(char *buf, size_t size, uint64_t addr, uint64_t key) {
            int ret = SAFE_CALL(fi_read(ep, buf, size, fi_mr_desc(mr), ~0, addr, key, nullptr));
            DO_LOG(INFO) << "Read sent: " << ret;
            SAFE_CALL(wait_for_completion(tx_cq));
        }*/

    private:
        bool is_server;
        const size_t MAX_MSG_SIZE = 4096;
        uint64_t msg_sends = 0;

        // These need to be closed by fabric
        fi_info *hints, *info;
        fid_fabric *fab;
        fid_domain *domain;
        fid_pep *pep;
        fid_eq *eq;
        fid_ep *ep;
        fid_cq *rx_cq, *tx_cq;
        std::map<uint64_t, fid_mr *> *mrs = new std::map<uint64_t, fid_mr *>();

        // Based on connectionless.hh, but not identical. This returns the value from fi_cq_read. 
        inline int wait_for_completion(struct fid_cq *cq) {
            fi_cq_msg_entry entry = {};
            int ret;
            while (1) {
                ret = fi_cq_read(cq, &entry,
                                 1); // TODO an rma write will likely cause the rx_cq to receive something, so I have to be careful about that.
                if (ret > 0) {
                    DO_LOG(TRACE) << "Entry flags " << entry.flags;
                    DO_LOG(TRACE) << "Entry rma " << (entry.flags & FI_RMA);
                    DO_LOG(TRACE) << "Entry len " << entry.len;
                    DO_LOG(TRACE) << "Entry ops " << entry.op_context;
                    return ret;
                }
                if (ret != -FI_EAGAIN) {
                    // New error on queue
                    struct fi_cq_err_entry err_entry = {};
                    fi_cq_readerr(cq, &err_entry, 0);
                    DO_LOG(ERROR) << fi_cq_strerror(cq, err_entry.prov_errno, err_entry.err_data, nullptr, 0);
                    return ret;
                }
            }
        }

        inline fid_mr *create_mr(char *buf, size_t size, uint64_t access, uint64_t &key) {
            fid_mr *mr = nullptr;
            DO_LOG(TRACE) << "Registering memory region starting at " << (void *) buf;
            SAFE_CALL(fi_mr_reg(domain, buf, size, access, 0, key, 0, &mr, nullptr));
            // need to wait on creation to finish
            DO_LOG(INFO) << "MR KEY: " << fi_mr_key(mr) << " for buffer starting at " << (void *) buf;
            key = fi_mr_key(mr);
            return mr;
        }

        /**
         * Allocates hints, and sets the correct settings for the connection.
         *
         * Requires nothing to be set
         **/
        inline void create_hints(uint32_t protocol) {
            hints = fi_allocinfo();
            hints->caps = FI_MSG | FI_RMA | FI_ATOMIC;
            hints->ep_attr->type = FI_EP_MSG;
            hints->ep_attr->protocol = protocol;
            hints->domain_attr->mr_mode = FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_VIRT_ADDR;
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
            DO_LOG(TRACE) << "Opening event queue";
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
            DO_LOG(TRACE) << "Creating tx completion queue";
            SAFE_CALL(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            cq_attr.size = info->rx_attr->size;
            DO_LOG(TRACE) << "Creating rx completion queue";
            SAFE_CALL(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));
            DO_LOG(TRACE) << "Binding TX CQ to EP";
            SAFE_CALL(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
            DO_LOG(TRACE) << "Binding RX CQ to EP";
            SAFE_CALL(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));
        }

        /**
         * Performs a blocking read of the event queue until an FI_CONNECTED event is triggered.
         *
         * @return true on success
         **/
        inline bool wait_for_eq_connected() {
            struct fi_eq_cm_entry entry = {};
            uint32_t event = 0;
            DO_LOG(TRACE) << "Reading eq for FI_CONNECTED event";
            int addr_len = fi_eq_sread(eq, &event, &entry, sizeof(entry), -1, 0);
            if (addr_len < 1) {
                struct fi_eq_err_entry err_entry = {};
                fi_eq_readerr(eq, &err_entry, 0);
                DO_LOG(ERROR) << fi_eq_strerror(eq, err_entry.prov_errno, err_entry.err_data, nullptr, 0);
            }
            if (event != FI_CONNECTED || addr_len < 0) {
                DO_LOG(ERROR) << "Not a connected event";
                return false;
            }
            DO_LOG(DEBUG) << "Connected";
            return true;
        }

        /**
         * Creates the domain, endpoint, counters, binds the event queue, and enables the ep.
         *
         * Requires fab, info to be set.
         *
         * @return true on success
         **/
        inline bool try_setup_active_ep() {
            LOG2<TRACE>() << "Creating domain";
            int ret = ERRREPORT(fi_domain(fab, info, &domain, nullptr));
            if (ret < 0) {
                return false;
            }

            LOG2<TRACE>() << "Creating active endpoint";
            ret = ERRREPORT(fi_endpoint(domain, info, &ep, nullptr));
            if (ret < 0) {
                return false;
            }

            setup_cqs();

            LOG2<TRACE>() << "Binding eq to pep";
            ret = ERRREPORT(fi_ep_bind(ep, &eq->fid, 0));
            if (ret < 0) {
                return false;
            }

            LOG2<TRACE>() << "Enabling endpoint";
            ret = ERRREPORT(fi_enable(ep));
            if (ret < 0) {
                return false;
            }
            return true;
        }

        /**
         * Creates the domain, endpoint, counters, binds the event queue, and enables the ep.
         *
         * Requires fab, info to be set.
         **/
        inline void setup_active_ep() {
            DO_LOG(TRACE) << "Creating domain";
            SAFE_CALL(fi_domain(fab, info, &domain, nullptr));

            DO_LOG(TRACE) << "Creating active endpoint";
            SAFE_CALL(fi_endpoint(domain, info, &ep, nullptr));

            setup_cqs();

            DO_LOG(TRACE) << "Binding eq to pep";
            SAFE_CALL(fi_ep_bind(ep, &eq->fid, 0));

            DO_LOG(TRACE) << "Enabling endpoint";
            SAFE_CALL(fi_enable(ep));
        }

        inline void createPep() {
            DO_LOG(TRACE) << "Creating passive endpoint";
            ERRCHK(fi_passive_ep(fab, info, &pep, nullptr));
            DO_LOG(TRACE) << "Binding eq to pep";
            ERRCHK(fi_pep_bind(pep, &eq->fid, 0));
            assert(pep);
            DO_LOG(TRACE) << "Transitioning pep to listening state";
            ERRCHK(fi_listen(pep));
        }

    };


    /**
     * Performs best effort broadcast
     * @param clients clients to send to
     * @param message message to send
     * @param messageSize size of message
     */
    [[deprecated("Use with unique_buf instead")]]
    inline void bestEffortBroadcast(std::vector<Connection> &connections, const char *message, size_t messageSize) {
        for (auto &c : connections) {
            c.send(message, messageSize);
        }
    }

    /**
     * Performs best effort broadcast
     * @param clients clients to send to
     * @param message message to send
     * @param messageSize size of message
     */
    template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
    inline void bestEffortBroadcast(std::vector<Connection> &connections, buf_t &message, size_t messageSize) {
        for (auto &c : connections) {
            c.send(message, messageSize);
        }
    }


    /**
     * Performs best effort broadcast recieve from client
     * @param clients clients to recv from
     * @param buf buffer
     * @param sizeOfBuf buffer size
     */
    inline void bestEffortBroadcastReceiveFrom(Connection &connections, char *buf, size_t sizeOfBuf) {
        connections.recv(buf, sizeOfBuf);
    }

    /**
     * Performs best effort broadcast recieve from client
     * @param clients clients to recv from
     * @param buf buffer
     * @param sizeOfBuf buffer size
     */
    template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
    inline void bestEffortBroadcastReceiveFrom(Connection &connections, buf_t &buf, size_t sizeOfBuf) {
        connections.recv(buf, sizeOfBuf);
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
     * Reliably broadcast from a server
     * @param clients clients to send to
     * @param message message to send
     * @param messageSize size of message
     */
    template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
    inline void reliableBroadcast(std::vector<Connection> &connections, buf_t &message, size_t messageSize) {
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

        receiveFrom.recv(buf, bufSize);

        if (!checkIfReceivedBefore(buf, bufSize)) {
            bestEffortBroadcast(connections, buf, bufSize);
            markAsReceived(buf, bufSize);
            return true;
        }
        return false;
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
    template<typename buf_t, std::enable_if_t<!std::is_same<char *, buf_t>::value> * = nullptr>
    inline bool
    reliableBroadcastReceiveFrom(Connection &receiveFrom, std::vector<Connection> &connections,
                                 buf_t &buf,
                                 const std::function<bool(buf_t &, size_t)> &checkIfReceivedBefore,
                                 const std::function<void(buf_t &, size_t)> &markAsReceived) {

        receiveFrom.recv(buf, buf.size());

        DO_LOG(DEBUG) << "Received message in broadcast";

        if (!checkIfReceivedBefore(buf, buf.size())) {
            bestEffortBroadcast(connections, buf, buf.size());
            markAsReceived(buf, buf.size());
            DO_LOG(DEBUG) << "Delivered message in broadcast";
            return true;
        }
        return false;

    }

};