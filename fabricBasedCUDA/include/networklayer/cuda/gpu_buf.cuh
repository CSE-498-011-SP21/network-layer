/**
 * @file
 */

#ifndef NETWORKLAYER_GPU_BUF_HH
#define NETWORKLAYER_GPU_BUF_HH

#include <cstddef>
#include <cstring>
#include <rdma/fi_rma.h>
#include <cassert>
#include <string>

namespace cse498 {

    /**
     * Wrapper to help with using this
     */
    class gpu_buf {
    public:

        /**
         * Allocate buffer
         */
        gpu_buf() : k_buf(nullptr), h_buf(nullptr), s_(0) {}

        gpu_buf(char *k, char *h, size_t s) : k_buf(k), h_buf(h), s_(s) {}

        gpu_buf(const gpu_buf &) = delete;

        gpu_buf(gpu_buf &&other) = default;

        ~gpu_buf() {}

        /**
         * Index into buffer
         * @param idx
         * @return
         */
        inline char &operator[](size_t idx) {
            return h_buf[idx];
        }

        /**
         * Index into buffer
         * @param idx
         * @return
         */
        inline const char &operator[](size_t idx) const {
            return h_buf[idx];
        }

        inline void moveToGPU() const {
            if (cudaMemcpy(k_buf, h_buf, s_, cudaMemcpyHostToDevice) != cudaSuccess) {
                exit(1);
            }
        }

        inline void moveToCPU() const {
            if (cudaMemcpy(h_buf, k_buf, s_, cudaMemcpyDeviceToHost) != cudaSuccess) {
                exit(1);
            }
        }

        /**
         * Copy to buffer
         * @param input
         * @param s
         * @param offset
         */
        inline void cpyTo(const char *input, size_t s, size_t offset = 0) {
            memcpy(h_buf + offset, input, s);
            moveToGPU();
        }

        /**
         * Copy from kernel buffer
         * @param output
         * @param s
         * @param offset
         */
        inline void cpyFrom(char *output, size_t s, size_t offset = 0) const {
            moveToCPU();
            memcpy(output, h_buf + offset, s);
        }

        /**
         * Set buffer starting at an offset of 0 with a string.
         * @param s
         * @return
         */
        inline gpu_buf &operator=(const std::string &s) {
            memcpy(h_buf, s.c_str(), s.size() + 1);
            moveToGPU();
            return *this;
        }

        /**
         * This potentially breaks safety, but it may be useful.
         * Ideally avoid this.
         * @return underlying buffer
         */
        inline char *get() {
            DO_LOG(TRACE) << "Returning " << k_buf;
            return k_buf;
        }

        inline char *getCPU() {
            return h_buf;
        }

        /**
         * This potentially breaks safety, but it may be useful.
         * Ideally avoid this.
         * @return underlying buffer
         */
        [[nodiscard]] inline const char *get() const {
            DO_LOG(TRACE) << "Returning " << k_buf;
            return k_buf;
        }

        [[nodiscard]] inline const char *getGPU() const {
            return h_buf;
        }

        /**
         * Callback for when memory is registered
         * @param key
         * @param d
         */
        inline void registerMemoryCallback(uint64_t key, void *d) {
            registered = true;
            key_ = key;
            desc = d;
        }

        /**
         * Key associated with buffer
         * @return
         */
        [[nodiscard]] inline uint64_t key() const {
            assert(registered);
            return key_;
        }

        /**
         * Size of buffer
         * @return
         */
        [[nodiscard]] inline size_t size() const {
            return s_;
        }

        /**
         * Descriptor for buffer
         * @return
         */
        [[nodiscard]] inline void *getDesc() {
            return desc;
        }

        /**
         * Returns if the buffer has been registered
         * @return
         */
        [[nodiscard]] inline bool isRegistered() const {
            return registered;
        }

    private:
        char *h_buf, *k_buf;
        const size_t s_;
        bool registered = false;
        uint64_t key_;
        void *desc = nullptr;
    };

}

#endif //NETWORKLAYER_GPU_BUF_HH
