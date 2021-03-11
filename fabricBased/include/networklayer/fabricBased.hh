/**
 * @file
 */

#pragma once

#include <networklayer/RPC.hh>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <atomic>
#include <unordered_map>

#include <kvcg_log2.hh>

#define ERRCHK(x) error_check((x), __FILE__, __LINE__);

inline void error_check(int err, std::string file, int line) {
    if (err) {
        DO_LOG(ERROR) << "ERROR (" << err << "): " << fi_strerror(-err) << " " << file << ":" << line;
        _exit(1);
    }
}

#define MAJOR_VERSION_USED 1
#define MINOR_VERSION_USED 9

static_assert(FI_MAJOR_VERSION == MAJOR_VERSION_USED && FI_MINOR_VERSION == MINOR_VERSION_USED, "Rely on libfabric 1.9");

namespace cse498 {

    /**
     * Default port for RPCs
     */
    const int DEFAULT_PORT = 8080;

    /*
     * Protocol:
     * Client Sends:
     * uint64_t addrlen
     * bytes of addr
     * Header
     * bytes of payload
     *
     * Server Sends:
     * uint64_t size of payload
     * bytes of payload
     */

    /**
     * Header for RPC
     */
    struct Header {
        /**
         * Function ID number
         */
        uint64_t fnID;
        /**
         * Size of argument to be sent
         */
        uint64_t sizeOfArg;
    };

    /**
     * Waits for something to happen in the cq.
     * @param cq The CQ we are waiting on
     * @return error
     */
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
                return ret;
            }
        }
    }


    /**
     * FabricRPC class. Currently a connectionless server.
     */
    class FabricRPC final : public RPC {
    public:
        /**
         * Create server. Default mapping of function 0 to shutdown.
         * @param fabricAddress Utilize this address
         */
        FabricRPC(const char *fabricAddress, uint32_t protocol = FI_PROTO_SOCK_TCP) : fnMap(
                new std::unordered_map<uint64_t, std::function<pack_t(pack_t)>>()) {

            done = false;

            registerRPC(0, [this](pack_t p) {
                done = true;
                return p;
            });

            hints = fi_allocinfo();
            hints->caps = FI_MSG;
            hints->ep_attr->type = FI_EP_RDM;
            hints->ep_attr->protocol = protocol;

            ERRCHK(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED), fabricAddress,
                              std::to_string(DEFAULT_PORT).c_str(), FI_SOURCE, hints, &fi));
            ERRCHK(fi_fabric(fi->fabric_attr, &fabric, nullptr));
            ERRCHK(fi_domain(fabric, fi, &domain, NULL));
            memset(&cq_attr, 0, sizeof(cq_attr));
            cq_attr.wait_obj = FI_WAIT_NONE;
            //cq_attr.format = FI_CQ_FORMAT_CONTEXT;
            cq_attr.size = fi->tx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            cq_attr.size = fi->rx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

            // Create an address vector. This allows connectionless endpoints to communicate
            // without having to resolve addresses, such as IPv4, during data transfers.
            memset(&av_attr, 0, sizeof(av_attr));
            av_attr.type = fi->domain_attr->av_type ?
                           fi->domain_attr->av_type : FI_AV_MAP;
            av_attr.count = 1;
            av_attr.name = NULL;
            ERRCHK(fi_av_open(domain, &av_attr, &av, NULL));

            ERRCHK(fi_endpoint(domain, fi, &ep, NULL));

            // Could create multiple endpoints, especially if there are multiple NICs available.

            // malloc buffers
            local_buf = new char[max_msg_size];
            remote_buf = new char[max_msg_size];

            memset(remote_buf, 0, max_msg_size);

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));

            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));


            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));

            // Enable EP
            ERRCHK(fi_enable(ep));

            // Register memory region for RDMA
            ERRCHK(fi_mr_reg(domain, remote_buf, max_msg_size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));
        }

        /**
         * Destructor
         */
        ~FabricRPC() {
            fi_freeinfo(hints);
            fi_freeinfo(fi);

            ERRCHK(fi_close(&mr->fid));
            ERRCHK(fi_close(&ep->fid));
            ERRCHK(fi_close(&tx_cq->fid));
            ERRCHK(fi_close(&rx_cq->fid));
            ERRCHK(fi_close(&av->fid));
            ERRCHK(fi_close(&fabric->fid));
            ERRCHK(fi_close(&domain->fid));

            delete[] local_buf;
            delete[] remote_buf;
        }

        /**
         * Register RPC with an id number and a function taking in pack_t and returning a pack_t
         * @param fnID id number
         * @param fn RPC function
         */
        inline void registerRPC(uint64_t fnID, std::function<pack_t(pack_t)> fn) {
            LOG2<DEBUG>() << "Registering " << fnID;
            assert(fnMap->insert({fnID, fn}).second);
        }

        /**
         * Start the server.
         */
        inline void start() {
            while (!done) {

                ERRCHK(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
                ERRCHK(wait_for_completion(rx_cq));

                uint64_t sizeOfAddress = *(uint64_t *) remote_buf;

                fi_addr_t remote_addr;

                if (1 != fi_av_insert(av, remote_buf + sizeof(uint64_t), 1, &remote_addr, 0, NULL)) {
                    std::cerr << "ERROR - fi_av_insert did not return 1" << std::endl;
                    perror("Error");
                    exit(1);
                }

                Header h = *(Header *) (remote_buf + sizeof(uint64_t) + sizeOfAddress);
                auto fnRes = fnMap->find(h.fnID);

                LOG2<DEBUG>() << "Getting fn " << h.fnID;

                assert(fnRes != fnMap->end());

                pack_t p(h.sizeOfArg);

                memcpy(p.data(), remote_buf + sizeof(uint64_t) + sizeOfAddress + sizeof(Header), h.sizeOfArg);

                auto res = fnRes->second(p);

                uint64_t size = res.size();

                memcpy(local_buf, (char *) &size, sizeof(uint64_t));
                memcpy(local_buf + sizeof(uint64_t), res.data(), size);

                ERRCHK(fi_send(ep, local_buf, res.size() + sizeof(uint64_t), nullptr, remote_addr, nullptr));
                ERRCHK(wait_for_completion(tx_cq));
                ERRCHK(fi_av_remove(av, &remote_addr, 1, 0));
            }
        }

    private:


        std::unordered_map<uint64_t, std::function<pack_t(pack_t)>> *fnMap;

        fi_info *fi, *hints;
        fid_fabric *fabric;
        fid_domain *domain;
        fi_cq_attr cq_attr;
        fi_av_attr av_attr;
        fid_av *av;
        fid_cq *tx_cq, *rx_cq;
        fid_ep *ep;
        size_t max_msg_size = 4096;
        fid_mr *mr;
        char *local_buf;
        char *remote_buf;
        std::atomic_bool done;
    };

    /**
     * RPC client using libfabric
     */
    class FabricRPClient final : public RPClient {
    public:
        /**
         * Create client
         * @param address connect to this address
         * @param port connect to this port
         */
        FabricRPClient(const std::string &address, uint16_t port, uint32_t protocol = FI_PROTO_SOCK_TCP) {
            hints = fi_allocinfo();
            hints->caps = FI_MSG;
            hints->ep_attr->type = FI_EP_RDM;
            hints->ep_attr->protocol = protocol;

            ERRCHK(fi_getinfo(FI_VERSION(MAJOR_VERSION_USED, MINOR_VERSION_USED), address.c_str(),
                              std::to_string(DEFAULT_PORT).c_str(), 0, hints, &fi));
            ERRCHK(fi_fabric(fi->fabric_attr, &fabric, nullptr));
            ERRCHK(fi_domain(fabric, fi, &domain, NULL));
            memset(&cq_attr, 0, sizeof(cq_attr));
            cq_attr.wait_obj = FI_WAIT_NONE;
            //cq_attr.format = FI_CQ_FORMAT_CONTEXT;
            cq_attr.size = fi->tx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &tx_cq, NULL));
            cq_attr.size = fi->rx_attr->size;
            ERRCHK(fi_cq_open(domain, &cq_attr, &rx_cq, NULL));

            // Create an address vector. This allows connectionless endpoints to communicate
            // without having to resolve addresses, such as IPv4, during data transfers.

            memset(&av_attr, 0, sizeof(av_attr));
            av_attr.type = fi->domain_attr->av_type;
            av_attr.count = 1;
            ERRCHK(fi_av_open(domain, &av_attr, &av, NULL));
            ERRCHK(fi_endpoint(domain, fi, &ep, NULL));

            // Could create multiple endpoints, especially if there are multiple NICs available.

            // malloc buffers
            remote_buf = new char[max_msg_size];
            local_buf = new char[max_msg_size];

            memset(remote_buf, 0, max_msg_size);

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));

            // Enable EP
            ERRCHK(fi_enable(ep));

            // Register memory region for RDMA
            ERRCHK(fi_mr_reg(domain, remote_buf, max_msg_size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));

            if (1 != fi_av_insert(av, fi->dest_addr, 1, &remote_addr, 0, NULL)) {
                exit(1);
            }

        }

        /**
         * Destructor
         */
        ~FabricRPClient() {
            fi_freeinfo(hints);
            fi_freeinfo(fi);

            ERRCHK(fi_close(&mr->fid));
            ERRCHK(fi_close(&ep->fid));
            ERRCHK(fi_close(&tx_cq->fid));
            ERRCHK(fi_close(&rx_cq->fid));
            ERRCHK(fi_close(&av->fid));
            ERRCHK(fi_close(&fabric->fid));
            ERRCHK(fi_close(&domain->fid));

            delete[] local_buf;
            delete[] remote_buf;
        }

        /**
         * Call remote function by sending data
         * @param fnID RPC id number
         * @param data data to send
         * @return pack_t returned by remote function
         */
        inline pack_t callRemote(uint64_t fnID, pack_t data) {

            assert(sizeof(size_t) == sizeof(uint64_t));

            size_t addrlen = 0;
            fi_getname(&ep->fid, nullptr, &addrlen);
            char *addr = new char[addrlen];
            ERRCHK(fi_getname(&ep->fid, addr, &addrlen));

            memcpy(local_buf, &addrlen, sizeof(uint64_t));
            memcpy(local_buf + sizeof(uint64_t), addr, addrlen);

            Header h;
            h.sizeOfArg = data.size();
            h.fnID = fnID;
            memcpy(local_buf + sizeof(uint64_t) + addrlen, (char *) &h, sizeof(Header));
            if (sizeof(uint64_t) + addrlen + data.size() + sizeof(Header) > max_msg_size) {
                exit(1);
            }
            memcpy(local_buf + sizeof(uint64_t) + addrlen + sizeof(Header), data.data(), data.size());

            ERRCHK(fi_send(ep, local_buf, data.size() + sizeof(uint64_t) + addrlen + data.size() + sizeof(Header),
                           nullptr, remote_addr, nullptr));
            ERRCHK(wait_for_completion(tx_cq));

            ERRCHK(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
            ERRCHK(wait_for_completion(rx_cq));

            uint64_t size = *(uint64_t *) remote_buf;

            pack_t p(remote_buf + sizeof(uint64_t), remote_buf + sizeof(uint64_t) + size);

            return p;
        }

    private:
        fi_addr_t remote_addr;
        fi_info *fi, *hints;
        fid_fabric *fabric;
        fid_domain *domain;
        fi_cq_attr cq_attr;
        fi_av_attr av_attr;
        fid_av *av;
        fid_cq *tx_cq, *rx_cq;
        fid_ep *ep;
        size_t max_msg_size = 4096;
        fid_mr *mr;
        char *local_buf;
        char *remote_buf;

    };
}