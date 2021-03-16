/**
 * @file
 */
#include <unistd.h>
#include <cassert>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <atomic>

#include <kvcg_log2.hh>

#ifndef NETWORKLAYER_CONNECTIONLESS_HH
#define NETWORKLAYER_CONNECTIONLESS_HH

#define ERRCHK(x) error_check_2((x), __FILE__, __LINE__);

inline void error_check_2(int err, std::string file, int line) {
    if (err) {
        LOG2<ERROR>() << "ERROR (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        _exit(1);
    }
}

#define ERRREPORT(x) error_report((x), __FILE__, __LINE__);

inline bool error_report(int err, std::string file, int line) {
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

    /*
     * Memory region handler type
     */
    using mr_t = fid_mr *;

    using addr_t = fi_addr_t;

    /**
     * Free an memory region handler
     * @param x memory region handler
     */
    inline void free_mr(mr_t x) {
        ERRCHK(fi_close(&x->fid));
    }

    /**
     * ConnectionlessServer
     */
    class ConnectionlessServer {
    public:
        /**
         * Constructor
         * @param fabricAddress address of server
         * @param port port to use
         */
        ConnectionlessServer(const char *fabricAddress, int port, uint32_t protocol = FI_PROTO_SOCK_TCP) {

            done = false;

            LOG2<TRACE>() << "Getting fi provider";
            hints = fi_allocinfo();
            hints->caps = FI_MSG | FI_TAGGED | FI_DIRECTED_RECV;
            hints->ep_attr->type = FI_EP_RDM;
            hints->ep_attr->protocol = protocol;

            ERRCHK(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED), fabricAddress,
                              std::to_string(port).c_str(), FI_SOURCE, hints, &fi));
            LOG2<DEBUG>() << "Using provider: " << fi->fabric_attr->prov_name;
            LOG2<DEBUG>() << "SRC ADDR: " << fi->fabric_attr->name;
            LOG2<TRACE>() << "Creating fabric object";
            ERRCHK(fi_fabric(fi->fabric_attr, &fabric, nullptr));
            LOG2<TRACE>() << "Creating domain";
            ERRCHK(fi_domain(fabric, fi, &domain, NULL));
            LOG2<TRACE>() << "Creating tx completion queue";
            memset(&cq_attr, 0, sizeof(cq_attr));
            cq_attr.wait_obj = FI_WAIT_NONE;
            cq_attr.format = FI_CQ_FORMAT_TAGGED;
            cq_attr.size = fi->tx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            LOG2<TRACE>() << "Creating rx completion queue";
            cq_attr.size = fi->rx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

            // Create an address vector. This allows connectionless endpoints to communicate
            // without having to resolve addresses, such as IPv4, during data transfers.
            LOG2<TRACE>() << "Creating address vector";
            memset(&av_attr, 0, sizeof(av_attr));
            av_attr.type = fi->domain_attr->av_type ?
                           fi->domain_attr->av_type : FI_AV_MAP;
            av_attr.count = 1;
            av_attr.name = NULL;
            ERRCHK(fi_av_open(domain, &av_attr, &av, NULL));

            LOG2<TRACE>() << "Creating endpoint";
            ERRCHK(fi_endpoint(domain, fi, &ep, NULL));

            // Could create multiple endpoints, especially if there are multiple NICs available.

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));

            LOG2<TRACE>() << "Binding Rx CQ to EP";
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));


            LOG2<TRACE>() << "Binding Tx CQ to EP";
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));

            // Enable EP
            LOG2<TRACE>() << "Enabling EP";
            ERRCHK(fi_enable(ep));

        }

        /**
         * Destructor
         */
        ~ConnectionlessServer() {
            fi_freeinfo(hints);
            fi_freeinfo(fi);

            ERRCHK(fi_close(&ep->fid));
            ERRCHK(fi_close(&tx_cq->fid));
            ERRCHK(fi_close(&rx_cq->fid));
            ERRCHK(fi_close(&av->fid));
            ERRCHK(fi_close(&fabric->fid));
            ERRCHK(fi_close(&domain->fid));

            LOG2<TRACE>() << ("All freed");
        }

        /**
         * Recv an address, must be coupled with a send addr
         * @param buf registered buffer
         * @param size
         * @return address
         */
        inline addr_t accept(char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting recv";

            bool b = false;
            do {
                b = ERRREPORT(fi_trecv(ep, buf, size, nullptr, FI_ADDR_UNSPEC, 1, 0, nullptr));
            } while (!b);

            ERRCHK(wait_for_completion(rx_cq));
            uint64_t sizeOfAddress = *(uint64_t *) buf;

            LOG2<TRACE>() << "Server: Adding client to AV";

            addr_t remote_addr;

            if (1 != fi_av_insert(av, buf + sizeof(uint64_t), 1, &remote_addr, 0, NULL)) {
                std::cerr << "ERROR - fi_av_insert did not return 1" << std::endl;
                perror("Error");
                exit(1);
            }
            LOG2<TRACE>() << "Server: Added client to AV";
            return remote_addr;
        }

        /**
         * Asynchronously accepts connection
         * @param buf registered buffer
         * @param size
         */
        inline void async_accept(char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting recv";
            bool b = false;
            do {
                b = ERRREPORT(fi_trecv(ep, buf, size, nullptr, FI_ADDR_UNSPEC, 1, 0, nullptr));
            } while (!b);
        }

        /**
         * Recv an address
         * @param buf registered buffer
         * @param size
         * @return address
         */
        inline addr_t wait_accept(char *buf, size_t size) {
            ERRCHK(wait_for_completion(rx_cq));
            uint64_t sizeOfAddress = *(uint64_t *) buf;

            LOG2<TRACE>() << "Server: Adding client to AV";

            addr_t remote_addr;

            if (1 != fi_av_insert(av, buf + sizeof(uint64_t), 1, &remote_addr, 0, NULL)) {
                std::cerr << "ERROR - fi_av_insert did not return 1" << std::endl;
                perror("Error");
                exit(1);
            }
            LOG2<TRACE>() << "Server: Added client to AV";
            return remote_addr;
        }

        /**
         * Recv an address, must be coupled with a send addr
         * @param buf registered buffer
         * @param size
         * @param remote_addr does not need to be pre-allocated
         */
        [[deprecated("Use accept instead")]]
        inline void recv_addr(char *buf, size_t size, addr_t &remote_addr) {
            remote_addr = accept(buf, size);
        }

        /**
         * Recv an address, must be coupled with a send addr
         * @param buf registered buffer (cannot be modified until wait_recv_addr)
         * @param size
         * @param remote_addr does not need to be pre-allocated
         */
        [[deprecated("Use async_accept instead")]]
        inline void async_recv_addr(char *buf, size_t size, addr_t &remote_addr) {
            async_accept(buf, size);
        }

        /**
         * Wait to receive an address after calling async_recv_addr
         * @param buf same registered buffer as before
         * @param size
         * @param remote_addr does not need to be pre-allocated
         */
        [[deprecated("Use wait_accept instead")]]
        inline void wait_recv_addr(char *buf, size_t size, addr_t &remote_addr) {
            remote_addr = wait_accept(buf, size);
        }

        /**
         * Recv message; crash on failure
         * @param remote_addr remote address
         * @param buf registered buffer
         * @param size size of buffer
         */
        inline void recv(addr_t remote_addr, char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting recv";
            ERRCHK(fi_trecv(ep, buf, size, nullptr, remote_addr, 2, 0, nullptr));
            ERRCHK(wait_for_completion(rx_cq));
        }

        /**
         * Recv message
         * @param remote_addr remote address
         * @param buf registered buffer
         * @param size size of buffer
         * @return true on success, false on failure
         */
        inline bool try_recv(addr_t remote_addr, char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting recv";
            bool b = ERRREPORT(fi_trecv(ep, buf, size, nullptr, remote_addr, 2, 0, nullptr));
            if (b) {
                ERRCHK(wait_for_completion(rx_cq));
                return true;
            }
            return false;
        }

        /**
         * Send message; crashes on failure
         * @param remote_addr remote address
         * @param buf not necessarily registered buffer
         * @param size size of buffer
         */
        inline void send(addr_t remote_addr, char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting send";
            ERRCHK(fi_tsend(ep, buf, size, nullptr, remote_addr, 2, nullptr));
            ERRCHK(wait_for_completion(tx_cq));
        }

        /**
         * Try to send a message
         * @param remote_addr
         * @param buf not necessarily registered buffer
         * @param size size of buffer
         * @return true on success, false on failure
         */
        inline bool try_send(addr_t remote_addr, char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting send";
            bool b = ERRREPORT(fi_tsend(ep, buf, size, nullptr, remote_addr, 2, nullptr));
            if (b) {
                ERRCHK(wait_for_completion(tx_cq));
                LOG2<TRACE>() << "Server: message sent";
                return true;
            }
            LOG2<TRACE>() << "Server: message send failed";
            return false;
        }

        /**
         * Asynchronously send message
         * @param remote_addr
         * @param buf
         * @param size
         * @return true on success, false on failure
         */
        inline bool async_send(addr_t remote_addr, char *buf, size_t size) {
            LOG2<TRACE>() << "Server: Posting send";
            return ERRREPORT(fi_tsend(ep, buf, size, nullptr, remote_addr, 2, nullptr));
        }

        /**
         * Wait for successful send to complete
         */
        inline void wait_send() {
            ERRCHK(wait_for_completion(tx_cq));
            LOG2<TRACE>() << "Server: Posting sent";
        }

        /**
         * Register buffer
         * @param buf buffer to register
         * @param size size of buffer
         * @param mr memory region object, not preallocated
         */
        inline void registerMR(char *buf, size_t size, mr_t &mr) {
            ERRCHK(fi_mr_reg(domain, buf, size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));
        }

        friend inline void
        bestEffortBroadcast(ConnectionlessServer &c, const std::vector<addr_t> &addresses, char *message,
                            size_t messageSize);

        friend inline void
        bestEffortBroadcastReceiveFrom(ConnectionlessServer &c, addr_t address, char *buf, size_t sizeOfBuf);

        friend inline bool
        reliableBroadcastReceiveFrom(ConnectionlessServer &server, addr_t recvFrom, std::vector<addr_t> &clients,
                                     char *buf,
                                     size_t bufSize, const std::function<bool(char *, size_t)> &checkIfReceivedBefore,
                                     const std::function<void(char *, size_t)> &markAsReceived);

    private:

        inline int wait_for_completion(struct fid_cq *cq) {
            fi_cq_entry entry;
            int ret;
            while (1) {
                ret = fi_cq_read(cq, &entry, 1);
                if (ret > 0) return 0;
                if (ret != -FI_EAGAIN) {
                    // New error on queue
                    struct fi_cq_err_entry err_entry;
                    fi_cq_readerr(cq, &err_entry, 0);
                    LOG2<TRACE>() << ("{0} {1}", fi_strerror(err_entry.err),
                            fi_cq_strerror(cq, err_entry.prov_errno,
                                           err_entry.err_data, NULL, 0));
                    return ret;
                }
            }
        }

        inline bool try_recv_tag(addr_t remote_addr, char *buf, size_t size, uint64_t tag) {
            LOG2<TRACE>() << "Server: Posting recv";
            bool b = ERRREPORT(fi_trecv(ep, buf, size, nullptr, remote_addr, tag, 0, nullptr));
            if (b) {
                ERRCHK(wait_for_completion(rx_cq));
                return true;
            }
            return false;
        }

        inline bool try_send_tag(addr_t remote_addr, char *buf, size_t size, uint64_t tag) {
            LOG2<TRACE>() << "Server: Posting send";
            bool b = ERRREPORT(fi_tsend(ep, buf, size, nullptr, remote_addr, tag, nullptr));
            if (b) {
                ERRCHK(wait_for_completion(tx_cq));
                LOG2<TRACE>() << "Server: message sent";
                return true;
            }
            LOG2<TRACE>() << "Server: message send failed";
            return false;
        }

        fi_info *fi, *hints;
        fid_domain *domain;
        fid_fabric *fabric;
        fi_cq_attr cq_attr;
        fi_av_attr av_attr;
        fid_av *av;
        fid_cq *tx_cq, *rx_cq;
        fid_ep *ep;
        size_t max_msg_size = 4096;
        std::atomic_bool done;
    };

    /**
     * ConnectionlessClient
     */
    class ConnectionlessClient {
    public:
        /**
         * Create client
         * @param address connect to this address
         * @param port connect to this port
         */
        ConnectionlessClient(const char *address, uint16_t port, uint32_t protocol = FI_PROTO_SOCK_TCP) {
            LOG2<TRACE>() << ("Getting fi provider");
            hints = fi_allocinfo();
            hints->caps = FI_MSG | FI_TAGGED;
            hints->ep_attr->type = FI_EP_RDM;
            hints->ep_attr->protocol = protocol;

            ERRCHK(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED), address,
                              std::to_string(port).c_str(), 0, hints, &fi));
            LOG2<TRACE>() << "Using provider: " << fi->fabric_attr->prov_name;
            LOG2<TRACE>() << "Creating fabric object";
            ERRCHK(fi_fabric(fi->fabric_attr, &fabric, nullptr));
            LOG2<TRACE>() << "Creating domain";
            ERRCHK(fi_domain(fabric, fi, &domain, NULL));
            LOG2<TRACE>() << "Creating tx completion queue";
            memset(&cq_attr, 0, sizeof(cq_attr));
            cq_attr.wait_obj = FI_WAIT_NONE;
            cq_attr.format = FI_CQ_FORMAT_TAGGED;
            cq_attr.size = fi->tx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            LOG2<TRACE>() << "Creating rx completion queue";
            cq_attr.size = fi->rx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

            // Create an address vector. This allows connectionless endpoints to communicate
            // without having to resolve addresses, such as IPv4, during data transfers.
            LOG2<TRACE>() << "Creating address vector";

            memset(&av_attr, 0, sizeof(av_attr));
            av_attr.type = fi->domain_attr->av_type;
            av_attr.count = 1;
            ERRCHK(fi_av_open(domain, &av_attr, &av, NULL));
            LOG2<TRACE>() << "Creating endpoint";
            ERRCHK(fi_endpoint(domain, fi, &ep, NULL));

            // Could create multiple endpoints, especially if there are multiple NICs available.

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));
            LOG2<TRACE>() << "Binding TX CQ to EP";
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
            LOG2<TRACE>() << "Binding RX CQ to EP";
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));

            // Enable EP
            LOG2<TRACE>() << "Enabling EP";
            ERRCHK(fi_enable(ep));

            if (1 != fi_av_insert(av, fi->dest_addr, 1, &remote_addr, 0, NULL)) {
                exit(1);
            }

        }

        /**
         * Destructor
         */
        ~ConnectionlessClient() {
            fi_freeinfo(hints);
            fi_freeinfo(fi);

            ERRCHK(fi_close(&ep->fid));
            ERRCHK(fi_close(&tx_cq->fid));
            ERRCHK(fi_close(&rx_cq->fid));
            ERRCHK(fi_close(&av->fid));
            ERRCHK(fi_close(&fabric->fid));
            ERRCHK(fi_close(&domain->fid));

        }

        /**
         * Connect to server
         * @param buf
         * @param size
         */
        inline void connect(char *buf, size_t size) {
            size_t addrlen = 0;
            fi_getname(&ep->fid, nullptr, &addrlen);
            char *addr = new char[addrlen];
            ERRCHK(fi_getname(&ep->fid, addr, &addrlen));

            LOG2<TRACE>() << "Client: Sending (" << addrlen << ") " << (void *) addr << " to " << remote_addr;

            memcpy(buf, &addrlen, sizeof(uint64_t));
            memcpy(buf + sizeof(uint64_t), addr, addrlen);
            LOG2<TRACE>() << "Client: Sending " << sizeof(uint64_t) + addrlen << "B in " << size << "B buffer";

            assert(size >= (sizeof(uint64_t) + addrlen));

            bool b = false;
            do {
                b = ERRREPORT(fi_tsend(ep, buf, sizeof(uint64_t) + addrlen, nullptr, remote_addr, 1, nullptr));
            } while (!b);
            ERRCHK(wait_for_completion(tx_cq));
            delete[] addr;
        }

        /**
         * Connect to server
         * @param buf
         * @param size
         * @return returns false on failure
         */
        inline bool async_connect(char *buf, size_t size) {
            size_t addrlen = 0;
            fi_getname(&ep->fid, nullptr, &addrlen);
            char *addr = new char[addrlen];
            ERRCHK(fi_getname(&ep->fid, addr, &addrlen));

            LOG2<TRACE>() << "Client: Sending (" << addrlen << ") " << (void *) addr << " to " << remote_addr;

            memcpy(buf, &addrlen, sizeof(uint64_t));
            memcpy(buf + sizeof(uint64_t), addr, addrlen);
            LOG2<TRACE>() << "Client: Sending " << sizeof(uint64_t) + addrlen << "B in " << size << "B buffer";

            assert(size >= (sizeof(uint64_t) + addrlen));
            delete[] addr;
            return ERRREPORT(fi_tsend(ep, buf, sizeof(uint64_t) + addrlen, nullptr, remote_addr, 1, nullptr));
        }

        /**
         * Wait for completion of connect.
         * @param buf
         * @param size
         */
        inline void wait_connect(char *buf, size_t size){
            ERRCHK(wait_for_completion(tx_cq));
        }


        /**
         * Send address
         * @param buf any buffer
         * @param size size of buffeer
         */
        [[deprecated("Use connect instead")]]
        inline void send_addr(char *buf, size_t size) {
            connect(buf, size);
        }

        /**
         * Send address
         * @param buf any buffer
         * @param size size of buffeer
         */
        [[deprecated("Use async_connect instead")]]
        inline bool async_send_addr(char *buf, size_t size, void *&state) {
            size_t addrlen = 0;
            fi_getname(&ep->fid, nullptr, &addrlen);
            char *addr = new char[addrlen];
            ERRCHK(fi_getname(&ep->fid, addr, &addrlen));

            LOG2<TRACE>() << "Client: Sending (" << addrlen << ") " << (void *) addr << " to " << remote_addr;

            memcpy(buf, &addrlen, sizeof(uint64_t));
            memcpy(buf + sizeof(uint64_t), addr, addrlen);
            LOG2<TRACE>() << "Client: Sending " << sizeof(uint64_t) + addrlen << "B in " << size << "B buffer";

            assert(size >= (sizeof(uint64_t) + addrlen));

            state = addr;

            return ERRREPORT(fi_tsend(ep, buf, sizeof(uint64_t) + addrlen, nullptr, remote_addr, 1, nullptr));
        }

        [[deprecated("Use wait_connect instead")]]
        inline void async_wait_send_addr(char *buf, size_t size, void *&state) {
            char *addr = (char *) state;
            ERRCHK(wait_for_completion(tx_cq));
            delete[] addr;
        }

        /**
         * Recv message
         * @param buf registered buffer
         * @param size size of buffer
         */
        inline void recv(char *buf, size_t size) {
            ERRCHK(fi_trecv(ep, buf, size, nullptr, remote_addr, 2, 0, nullptr));
            ERRCHK(wait_for_completion(rx_cq));
        }

        /**
         * Send message
         * @param buf any buffer
         * @param size size of buffer
         */
        inline void send(char *buf, size_t size) {
            ERRCHK(fi_tsend(ep, buf, size, nullptr, remote_addr, 2, nullptr));
            ERRCHK(wait_for_completion(tx_cq));
        }

        inline bool async_send(char *buf, size_t size) {
            LOG2<TRACE>() << "Client: Posting send";
            return ERRREPORT(fi_tsend(ep, buf, size, nullptr, remote_addr, 2, nullptr));
        }

        inline void wait_send() {
            ERRCHK(wait_for_completion(tx_cq));
            LOG2<TRACE>() << "Client: Posting sent";
        }

        /**
         * Register memory region
         * @param buf buffer
         * @param size size of buffer
         * @param mr memory region
         */
        inline void registerMR(char *buf, size_t size, mr_t &mr) {
            ERRCHK(fi_mr_reg(domain, buf, size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));
        }

        friend inline void
        bestEffortBroadcast(std::vector<ConnectionlessClient> &clients, char *message, size_t messageSize);

        friend inline void bestEffortBroadcastReceiveFrom(ConnectionlessClient &client, char *buf, size_t sizeOfBuf);

        friend inline bool
        reliableBroadcastReceiveFrom(ConnectionlessClient &receiveFrom, std::vector<ConnectionlessClient> &clients,
                                     char *buf,
                                     size_t bufSize, const std::function<bool(char *, size_t)> &checkIfReceivedBefore,
                                     const std::function<void(char *, size_t)> &markAsReceived);

    private:

        static inline int wait_for_completion(struct fid_cq *cq) {
            fi_cq_entry entry;
            int ret;
            while (1) {
                ret = fi_cq_read(cq, &entry, 1);
                if (ret > 0) return 0;
                if (ret != -FI_EAGAIN) {
                    // New error on queue
                    struct fi_cq_err_entry err_entry;
                    fi_cq_readerr(cq, &err_entry, 0);
                    LOG2<TRACE>() << fi_strerror(err_entry.err) << " " <<
                                  fi_cq_strerror(cq, err_entry.prov_errno,
                                                 err_entry.err_data, NULL, 0);
                    return ret;
                }
            }
        }

        inline bool try_recv_tag(char *buf, size_t size, uint64_t tag) {
            LOG2<TRACE>() << "Client: Posting recv";
            bool b = ERRREPORT(fi_trecv(ep, buf, size, nullptr, remote_addr, tag, 0, nullptr));
            if (b) {
                ERRCHK(wait_for_completion(rx_cq));
                LOG2<TRACE>() << "Client: message recv";
                return true;
            }
            return false;
        }

        inline bool try_send_tag(char *buf, size_t size, uint64_t tag) {
            LOG2<TRACE>() << "Client: Posting send";
            bool b = ERRREPORT(fi_tsend(ep, buf, size, nullptr, remote_addr, tag, nullptr));
            if (b) {
                ERRCHK(wait_for_completion(tx_cq));
                LOG2<TRACE>() << "Client: message sent";
                return true;
            }
            LOG2<TRACE>() << "Client: message send failed";
            return false;
        }

        fi_addr_t remote_addr;
        fi_info *fi, *hints;
        fid_domain *domain;
        fid_fabric *fabric;
        fi_cq_attr cq_attr;
        fi_av_attr av_attr;
        fid_av *av;
        fid_cq *tx_cq, *rx_cq;
        fid_ep *ep;
        size_t max_msg_size = 4096;
    };

    /**
     * Structure for simplifying programming with client and server
     */
    struct Connectionless_t {

        /**
         * Create connectionless type
         * @param useServer should this be a server?
         * @param addr address
         * @param port port
         */
        Connectionless_t(bool useServer, char *addr, int port, uint32_t protocol = FI_PROTO_SOCK_TCP) : isServer(
                useServer) {
            if (isServer) {
                this->server = new ConnectionlessServer(addr, port, protocol);
            } else {
                this->client = new ConnectionlessClient(addr, port, protocol);
            }
        }

        ~Connectionless_t() {
            if (isServer) {
                delete server;
            } else {
                delete client;
            }
        }

        bool isServer;
        union {
            ConnectionlessServer *server;
            ConnectionlessClient *client;
        };
    };

    /**
     * Performs best effort broadcast
     * @param c server
     * @param addresses addresses to send to
     * @param message message to send
     * @param messageSize size of message
     */
    inline void bestEffortBroadcast(ConnectionlessServer &c, const std::vector<addr_t> &addresses, char *message,
                                    size_t messageSize) {
        DO_LOG(TRACE) << "Sending best effort from server";

        for (auto &a : addresses) {
            while (!c.try_send_tag(a, message, messageSize, 3));
        }

        DO_LOG(TRACE) << "Sent best effort from server";
    }

    /**
     * Performs best effort broadcast
     * @param clients clients to send to
     * @param message message to send
     * @param messageSize size of message
     */
    inline void bestEffortBroadcast(std::vector<ConnectionlessClient> &clients, char *message, size_t messageSize) {
        DO_LOG(TRACE) << "Sending best effort from client";

        for (auto &c : clients) {
            while (!c.try_send_tag(message, messageSize, 3));
        }

        DO_LOG(TRACE) << "Sent best effort from client";
    }

    /**
     * Performs best effort broadcast recieve from client
     * @param clients clients to recv from
     * @param buf buffer
     * @param sizeOfBuf buffer size
     */
    inline void bestEffortBroadcastReceiveFrom(ConnectionlessClient &client, char *buf, size_t sizeOfBuf) {
        while (!client.try_recv_tag(buf, sizeOfBuf, 3));
        DO_LOG(TRACE) << "Recv best effort from client";

    }

    /**
     * Performs best effort broadcast recieve from client
     * @param c connection to recv from
     * @param address address to recv from
     * @param buf buffer
     * @param sizeOfBuf buffer size
     */
    inline void
    bestEffortBroadcastReceiveFrom(ConnectionlessServer &c, addr_t address, char *buf, size_t sizeOfBuf) {
        while (!c.try_recv_tag(address, buf, sizeOfBuf, 3));
        DO_LOG(TRACE) << "Recv best effort from server";
    }

    /**
     * Reliably broadcast from a server
     * @param c server
     * @param addresses to send to
     * @param message to send
     * @param messageSize size of message
     */
    inline void reliableBroadcast(ConnectionlessServer &c, const std::vector<addr_t> &addresses, char *message,
                                  size_t messageSize) {
        bestEffortBroadcast(c, addresses, message, messageSize);
    }


    /**
     * Reliably broadcast from a server
     * @param clients clients to send to
     * @param message message to send
     * @param messageSize size of message
     */
    inline void reliableBroadcast(std::vector<ConnectionlessClient> &clients, char *message, size_t messageSize) {
        bestEffortBroadcast(clients, message, messageSize);
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
    reliableBroadcastReceiveFrom(ConnectionlessClient &receiveFrom, std::vector<ConnectionlessClient> &clients,
                                 char *buf,
                                 size_t bufSize, const std::function<bool(char *, size_t)> &checkIfReceivedBefore,
                                 const std::function<void(char *, size_t)> &markAsReceived) {
        while (!receiveFrom.try_recv_tag(buf, bufSize, 3));

        if (!checkIfReceivedBefore(buf, bufSize)) {
            bestEffortBroadcast(clients, buf, bufSize);
            markAsReceived(buf, bufSize);
            return true;
        }
        return false;
    }

    /**
     * Receive from
     * @param server ConnectionlessServer to recv on
     * @param receiveFrom node to receive from
     * @param clients clients to send to
     * @param buf buffer to use (registered)
     * @param bufSize size of buffer to use
     * @param checkIfReceivedBefore function to check if the message has been received before
     * @param markAsReceived function to mark a message as received
     * @return true if it has not been received before
     */
    inline bool
    reliableBroadcastReceiveFrom(ConnectionlessServer &server, addr_t recvFrom, std::vector<addr_t> &clients,
                                 char *buf,
                                 size_t bufSize, const std::function<bool(char *, size_t)> &checkIfReceivedBefore,
                                 const std::function<void(char *, size_t)> &markAsReceived) {
        while (!server.try_recv_tag(recvFrom, buf, bufSize, 3));

        if (!checkIfReceivedBefore(buf, bufSize)) {
            bestEffortBroadcast(server, clients, buf, bufSize);
            markAsReceived(buf, bufSize);
            return true;
        }
        return false;
    }


}


#endif //NETWORKLAYER_CONNECTIONLESS_HH
