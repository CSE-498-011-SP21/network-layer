//
// Created by depaulsmiller on 3/9/21.
//

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>

#include <spdlog/spdlog.h>

#include <cstring>

#define SAFE_CALL(x) error_check_2((x), __FILE__, __LINE__);

inline void error_check_2(int err, std::string file, int line) {
    if (err) {
        SPDLOG_CRITICAL("ERROR ({0}): {1} {2}:{3}", err, fi_strerror(-err), file, line);
        _exit(1);
    }
}

static_assert(FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 6, "We require libfabric 1.6");

namespace cse498 {

	/**
	 * Free an memory region handler
	 * @param x memory region handler
	 */
	void free_mr(mr_t x){
	    ERRCHK(fi_close(&x->fid));
	}

	class Domain {
		public:

			Domain(fid_fabric *fabric, fi_info *info, void *context) {
				SPDLOG_TRACE("Creating domain");
				SAFE_CALL(fi_domain(fabric, info, &domain, context));
			}

			~Domain() {
				SPDLOG_TRACE("Closing all the fabric objects");
				fi_close(&domain->fid);
			}

			fid_domain get_domain() {
				return domain;
			}

			// // Maybe need?
			// //int fi_domain_bind(struct fid_domain *domain, struct fid *eq, uint64_t flags);
			// void domain_bind() {

			// }

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
			fid_domain *domain;

	};
};