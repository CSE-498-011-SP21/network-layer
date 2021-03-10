/**
 * @file
 */
#include <networklayer/RPC.hh>
#include <tbb/concurrent_unordered_map.h>
#include <cassert>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <atomic>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <spdlog/spdlog.h>

#ifndef NETWORKLAYER_CONNECTIONLESS_HH
#define NETWORKLAYER_CONNECTIONLESS_HH

#define ERRCHK(x) error_check_2((x), __FILE__, __LINE__);

inline void error_check_2(int err, std::string file, int line) {
    if (err) {
        SPDLOG_CRITICAL("ERROR ({0}): {1} {2}:{3}", err, fi_strerror(-err), file, line);
        _exit(1);
    }
}

namespace cse498 {

    /*
     * Memory region handler type
     */
    using mr_t = fid_mr*;

    using addr_t = fi_addr_t;

    /**
     * Free an memory region handler
     * @param x memory region handler
     */
    void free_mr(mr_t x){
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
        ConnectionlessServer(const char *fabricAddress, int port) {

            done = false;

            SPDLOG_TRACE("Getting fi provider");
            hints = fi_allocinfo();
            hints->caps = FI_MSG;
            hints->ep_attr->type = FI_EP_RDM;

            ERRCHK(fi_getinfo(FI_VERSION(1, 6), fabricAddress,
                              std::to_string(port).c_str(), FI_SOURCE, hints, &fi));
            SPDLOG_DEBUG("Using provider: {0}", fi->fabric_attr->prov_name);
            SPDLOG_DEBUG("SRC ADDR: {0}", fi->fabric_attr->name);
            SPDLOG_TRACE("Creating fabric object");
            ERRCHK(fi_fabric(fi->fabric_attr, &fabric, nullptr));
            SPDLOG_TRACE("Creating domain");
            ERRCHK(fi_domain(fabric, fi, &domain, NULL));
            SPDLOG_TRACE("Creating tx completion queue");
            memset(&cq_attr, 0, sizeof(cq_attr));
            cq_attr.wait_obj = FI_WAIT_NONE;
            //cq_attr.format = FI_CQ_FORMAT_CONTEXT;
            cq_attr.size = fi->tx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            SPDLOG_TRACE("Creating rx completion queue");
            cq_attr.size = fi->rx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

            // Create an address vector. This allows connectionless endpoints to communicate
            // without having to resolve addresses, such as IPv4, during data transfers.
            SPDLOG_TRACE("Creating address vector");
            memset(&av_attr, 0, sizeof(av_attr));
            av_attr.type = fi->domain_attr->av_type ?
                           fi->domain_attr->av_type : FI_AV_MAP;
            av_attr.count = 1;
            av_attr.name = NULL;
            ERRCHK(fi_av_open(domain, &av_attr, &av, NULL));

            SPDLOG_TRACE("Creating endpoint");
            ERRCHK(fi_endpoint(domain, fi, &ep, NULL));

            // Could create multiple endpoints, especially if there are multiple NICs available.

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));

            SPDLOG_TRACE("Binding Rx CQ to EP");
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));


            SPDLOG_TRACE("Binding Tx CQ to EP");
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));

            // Enable EP
            SPDLOG_TRACE("Enabling EP");
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

            SPDLOG_INFO("All freed");
        }

        /**
         * Recv an address, must be coupled with a send addr
         * @param buf registered buffer
         * @param size
         * @param remote_addr does not need to be pre-allocated
         */
        void recv_addr(char *buf, size_t size, addr_t &remote_addr) {
            SPDLOG_TRACE("Server: Posting recv");

            ERRCHK(fi_recv(ep, buf, size, nullptr, 0, nullptr));
            ERRCHK(wait_for_completion(rx_cq));
            uint64_t sizeOfAddress = *(uint64_t *) buf;

            SPDLOG_TRACE("Server: Adding client to AV");

            if (1 != fi_av_insert(av, buf + sizeof(uint64_t), 1, &remote_addr, 0, NULL)) {
                std::cerr << "ERROR - fi_av_insert did not return 1" << std::endl;
                perror("Error");
                exit(1);
            }
            SPDLOG_TRACE("Server: Added client to AV");
        }

        /**
         * Recv message
         * @param remote_addr remote address
         * @param buf registered buffer
         * @param size size of buffer
         */
        void recv(addr_t remote_addr, char *buf, size_t size) {
            ERRCHK(fi_recv(ep, buf, size, nullptr, remote_addr, nullptr));
            ERRCHK(wait_for_completion(rx_cq));
        }

        /**
         * Send message
         * @param remote_addr remote address
         * @param buf not necessarily registered buffer
         * @param size size of buffer
         */
        void send(addr_t remote_addr, char *buf, size_t size) {
            SPDLOG_TRACE("Server: Posting send");
            ERRCHK(fi_send(ep, buf, size, nullptr, remote_addr, nullptr));
            ERRCHK(wait_for_completion(tx_cq));
            SPDLOG_TRACE("Server: Posting sent");
        }

        /**
         * Register buffer
         * @param buf buffer to register
         * @param size size of buffer
         * @param mr memory region object, not preallocated
         */
        void registerMR(char* buf, size_t size, mr_t& mr){
            ERRCHK(fi_mr_reg(domain, buf, size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));
        }

    private:

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

        fi_info *fi, *hints;
        fid_domain* domain;
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
        ConnectionlessClient(const char* address, uint16_t port) {
            SPDLOG_TRACE("Getting fi provider");
            hints = fi_allocinfo();
            hints->caps = FI_MSG;
            hints->ep_attr->type = FI_EP_RDM;
            ERRCHK(fi_getinfo(FI_VERSION(1, 6), address,
                              std::to_string(port).c_str(), 0, hints, &fi));
            SPDLOG_DEBUG("Using provider: {}", fi->fabric_attr->prov_name);
            SPDLOG_TRACE("Creating fabric object");
            ERRCHK(fi_fabric(fi->fabric_attr, &fabric, nullptr));
            SPDLOG_TRACE("Creating domain");
            ERRCHK(fi_domain(fabric, fi, &domain, NULL));
            SPDLOG_TRACE("Creating tx completion queue");
            memset(&cq_attr, 0, sizeof(cq_attr));
            cq_attr.wait_obj = FI_WAIT_NONE;
            //cq_attr.format = FI_CQ_FORMAT_CONTEXT;
            cq_attr.size = fi->tx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            SPDLOG_TRACE("Creating rx completion queue");
            cq_attr.size = fi->rx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

            // Create an address vector. This allows connectionless endpoints to communicate
            // without having to resolve addresses, such as IPv4, during data transfers.
            SPDLOG_TRACE("Creating address vector");

            memset(&av_attr, 0, sizeof(av_attr));
            av_attr.type = fi->domain_attr->av_type;
            av_attr.count = 1;
            ERRCHK(fi_av_open(domain, &av_attr, &av, NULL));
            SPDLOG_TRACE("Creating endpoint");
            ERRCHK(fi_endpoint(domain, fi, &ep, NULL));

            // Could create multiple endpoints, especially if there are multiple NICs available.

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));
            SPDLOG_TRACE("Binding TX CQ to EP");
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
            SPDLOG_TRACE("Binding RX CQ to EP");
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));

            // Enable EP
            SPDLOG_TRACE("Enabling EP");
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
         * Send address
         * @param buf any buffer
         * @param size size of buffeer
         */
        void send_addr(char *buf, size_t size) {
            size_t addrlen = 0;
            fi_getname(&ep->fid, nullptr, &addrlen);
            char *addr = new char[addrlen];
            ERRCHK(fi_getname(&ep->fid, addr, &addrlen));

            SPDLOG_DEBUG("Client: Sending ({0}) {1} to {2}", addrlen, (void *) addr, remote_addr);

            memcpy(buf, &addrlen, sizeof(uint64_t));
            memcpy(buf + sizeof(uint64_t), addr, addrlen);
            SPDLOG_DEBUG("Client: Sending {0}B in {1}B buffer", sizeof(uint64_t) + addrlen, size);

            assert(size >= (sizeof(uint64_t) + addrlen));

            ERRCHK(fi_send(ep, buf, sizeof(uint64_t) + addrlen, nullptr, remote_addr, nullptr));
            ERRCHK(wait_for_completion(tx_cq));
        }

        /**
         * Recv message
         * @param buf registered buffer
         * @param size size of buffer
         */
        void recv(char *buf, size_t size) {
            ERRCHK(fi_recv(ep, buf, size, nullptr, remote_addr, nullptr));
            ERRCHK(wait_for_completion(rx_cq));
        }

        /**
         * Send message
         * @param buf any buffer
         * @param size size of buffer
         */
        void send(char *buf, size_t size) {
            ERRCHK(fi_send(ep, buf, size, nullptr, remote_addr, nullptr));
            ERRCHK(wait_for_completion(tx_cq));
        }

        /**
         * Register memory region
         * @param buf buffer
         * @param size size of buffer
         * @param mr memory region
         */
        void registerMR(char* buf, size_t size, mr_t& mr){
            ERRCHK(fi_mr_reg(domain, buf, size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));
        }

    private:

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

        fi_addr_t remote_addr;
        fi_info *fi, *hints;
        fid_domain* domain;
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
        Connectionless_t(bool useServer, char* addr, int port) : isServer(useServer) {
            if(isServer){
                this->server = new ConnectionlessServer(addr, port);
            } else {
                this->client = new ConnectionlessClient(addr, port);
            }
        }

        ~Connectionless_t(){
            if(isServer){
                delete server;
            } else {
                delete client;
            }
        }

        bool isServer;
        union {
            ConnectionlessServer* server;
            ConnectionlessClient* client;
        };
    };

}


#endif //NETWORKLAYER_CONNECTIONLESS_HH
