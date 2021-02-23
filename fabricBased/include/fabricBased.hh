//
// Created by depaulsmiller on 2/18/21.
//

//#include <boost/asio.hpp>
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

#define ERRCHK(x) error_check((x), __FILE__, __LINE__);

inline void error_check(int err, std::string file, int line) {
    if (err) {
        SPDLOG_CRITICAL("ERROR ({0}): {1} {2}:{3}", err, fi_strerror(-err), file, line);
        _exit(1);
    }
}

namespace cse498 {

    int DEFAULT_PORT = 8080;

    struct Header {
        uint64_t fnID;
        uint64_t sizeOfArg;
    };

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


    class FabricRPC final : public RPC {
    public:
        FabricRPC(const char* fabricAddress) : fnMap(new tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(pack_t)>>()) {

            done = false;

            assert(fnMap->insert({0, [this](pack_t p) {
                done = true;
                return p;
            }}).second);

            SPDLOG_TRACE("Getting fi provider");
            hints = fi_allocinfo();
            hints->caps = FI_MSG;
            hints->ep_attr->type = FI_EP_RDM;

            ERRCHK(fi_getinfo(FI_VERSION(1, 6), fabricAddress,
                              std::to_string(DEFAULT_PORT).c_str(), FI_SOURCE, hints, &fi));
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

            // malloc buffers
            local_buf = new char[max_msg_size];
            remote_buf = new char[max_msg_size];

            memset(remote_buf, 0, max_msg_size);

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));

            SPDLOG_TRACE("Binding Rx CQ to EP");
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));


            SPDLOG_TRACE("Binding Tx CQ to EP");
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));

            // Enable EP
            SPDLOG_TRACE("Enabling EP");
            ERRCHK(fi_enable(ep));

            // Register memory region for RDMA
            SPDLOG_TRACE("Registering memory region");
            ERRCHK(fi_mr_reg(domain, remote_buf, max_msg_size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));
            SPDLOG_TRACE("Server: Receiving client address");

            ERRCHK(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
            ERRCHK(wait_for_completion(rx_cq));
            SPDLOG_TRACE("Completion");
            fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, NULL);

            fi_addr_t remote_addr;
            SPDLOG_TRACE("Server: Adding client to AV");

            if (1 != fi_av_insert(av, remote_buf, 1, &remote_addr, 0, NULL)) {
                std::cerr << "ERROR - fi_av_insert did not return 1" << std::endl;
                perror("Error");
                exit(1);
            }
            // send ack
            SPDLOG_TRACE("Server: Sending ack");
            int err = -FI_EAGAIN;
            while (err == -FI_EAGAIN) {
                err = fi_send(ep, local_buf, 1, NULL, remote_addr, NULL);
                if (err && (err != -FI_EAGAIN)) {
                    perror("Error");
                    exit(1);
                }
            }
            SPDLOG_TRACE("Server: Waiting for Tx CQ completion");

            wait_for_completion(tx_cq);
            SPDLOG_TRACE("Sent");
        }

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
            SPDLOG_INFO("All freed");
        }

        /**
         * Register RPC with an id number and a function taking in pack_t and returning a pack_t
         * @param fnID id number
         * @param fn RPC function
         */
        void registerRPC(uint64_t fnID, std::function<pack_t(pack_t)> fn) {
            assert(fnMap->insert({fnID, fn}).second);
        }

        void start() {
            while (!done) {
                ERRCHK(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
                ERRCHK(wait_for_completion(rx_cq));

                Header h;

                h = *(Header *) remote_buf;

                auto fnRes = fnMap->find(h.fnID);

                pack_t p(h.sizeOfArg);

                memcpy(p.data(), remote_buf + sizeof(Header), h.sizeOfArg);

                auto res = fnRes->second(p);

                uint64_t size = res.size();

                memcpy(local_buf, (char *) &size, sizeof(uint64_t));
                memcpy(local_buf + sizeof(uint64_t), res.data(), size);

                ERRCHK(fi_send(ep, local_buf, res.size() + sizeof(uint64_t), nullptr, 0, nullptr));
                ERRCHK(wait_for_completion(tx_cq));
            }
        }

    private:


        tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(pack_t)>> *fnMap;

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

    class FabricRPClient final : public RPClient {
    public:
        FabricRPClient(const std::string &address, uint16_t port) {
            SPDLOG_TRACE("Getting fi provider");
            hints = fi_allocinfo();
            hints->caps = FI_MSG;
            hints->ep_attr->type = FI_EP_RDM;
            ERRCHK(fi_getinfo(FI_VERSION(1, 6), address.c_str(),
                              std::to_string(DEFAULT_PORT).c_str(), 0, hints, &fi));
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

            // malloc buffers
            remote_buf = new char[max_msg_size];
            local_buf = new char[max_msg_size];

            memset(remote_buf, 0, max_msg_size);

            ERRCHK(fi_ep_bind(ep, &av->fid, 0));
            SPDLOG_TRACE("Binding TX CQ to EP");
            ERRCHK(fi_ep_bind(ep, &tx_cq->fid, FI_TRANSMIT));
            SPDLOG_TRACE("Binding RX CQ to EP");
            ERRCHK(fi_ep_bind(ep, &rx_cq->fid, FI_RECV));

            // Enable EP
            SPDLOG_TRACE("Enabling EP");
            ERRCHK(fi_enable(ep));

            // Register memory region for RDMA
            SPDLOG_TRACE("Registering memory region");
            ERRCHK(fi_mr_reg(domain, remote_buf, max_msg_size,
                             FI_WRITE | FI_REMOTE_WRITE | FI_READ | FI_REMOTE_READ, 0,
                             0, 0, &mr, NULL));

            fi_addr_t remote_addr;
            if (1 != fi_av_insert(av, fi->dest_addr, 1, &remote_addr, 0, NULL)) {
                exit(1);
            }

            size_t addrlen = 0;
            fi_getname(&ep->fid, nullptr, &addrlen);
            char *addr = new char[addrlen];
            ERRCHK(fi_getname(&ep->fid, addr, &addrlen));

            SPDLOG_DEBUG("Client: Sending ({0}) {1} to {2}", addrlen, (void *) addr, remote_addr);
            int err = -FI_EAGAIN;
            do {
                err = fi_send(ep, addr, addrlen, NULL, remote_addr, NULL);
                if (err && (err != -FI_EAGAIN))
                    exit(1);
            } while (err == -FI_EAGAIN);
            wait_for_completion(tx_cq);

            SPDLOG_TRACE("Sent");
            ERRCHK(fi_recv(ep, remote_buf, max_msg_size, 0, 0, NULL));
            wait_for_completion(rx_cq);
            SPDLOG_TRACE("Received ack");
        }

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
        pack_t callRemote(uint64_t fnID, pack_t data) {
            Header h;
            h.sizeOfArg = data.size();
            h.fnID = fnID;
            memcpy(local_buf, (char *) &h, sizeof(Header));
            if (data.size() + sizeof(Header) > max_msg_size) {
                exit(1);
            }
            memcpy(local_buf + sizeof(Header), data.data(), data.size());

            ERRCHK(fi_send(ep, local_buf, data.size() + sizeof(Header), nullptr, 0, nullptr));
            ERRCHK(wait_for_completion(tx_cq));

            ERRCHK(fi_recv(ep, remote_buf, max_msg_size, nullptr, 0, nullptr));
            ERRCHK(wait_for_completion(rx_cq));

            uint64_t size = *(uint64_t *) remote_buf;

            pack_t p(remote_buf + sizeof(uint64_t), remote_buf + sizeof(uint64_t) + size);

            return p;
        }

    private:
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