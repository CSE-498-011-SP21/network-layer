/**
 * @file
 */

#ifndef NETWORKLAYER_UNIQUE_BUF_HH
#define NETWORKLAYER_UNIQUE_BUF_HH

#include <cstddef>
#include <cstring>
#include <rdma/fi_rma.h>
#include <cassert>
#include <string>

namespace cse498 {

    /**
     * Unique buffer; uses the semantics of unique_ptr<char[]>
     */
    class unique_buf {
    public:

        /**
         * Allocate buffer
         */
        unique_buf() : buf(new char[4096]), s_(4096) {}

        /**
         * Allocate buffer of size s
         * @param s size
         */
        explicit unique_buf(size_t s) : buf(new char[s]), s_(s) {}

        unique_buf(const unique_buf &) = delete;

        unique_buf(unique_buf &&other) = default;

        ~unique_buf() {
            delete[] buf;
        }

        /**
         * Index into buffer
         * @param idx
         * @return
         */
        inline char &operator[](size_t idx) {
            return buf[idx];
        }

        /**
         * Index into buffer
         * @param idx
         * @return
         */
        inline const char &operator[](size_t idx) const {
            return buf[idx];
        }

        /**
         * Copy to buffer
         * @param input
         * @param s
         * @param offset
         */
        inline void cpyTo(const char *input, size_t s, size_t offset = 0) {
            memcpy(buf + offset, input, s);
        }

        /**
         * Copy from buffer
         * @param output
         * @param s
         * @param offset
         */
        inline void cpyFrom(char *output, size_t s, size_t offset = 0) const {
            memcpy(output, buf + offset, s);
        }

        /**
         * Set buffer starting at an offset of 0 with a string.
         * @param s
         * @return
         */
        inline unique_buf &operator=(const std::string &s) {
            memcpy(buf, s.c_str(), s.size() + 1);
            return *this;
        }

        /**
         * This potentially breaks safety, but it may be useful.
         * Ideally avoid this.
         * @return underlying buffer
         */
        inline char *get() {
            return buf;
        }

        /**
         * This potentially breaks safety, but it may be useful.
         * Ideally avoid this.
         * @return underlying buffer
         */
        [[nodiscard]] inline const char *get() const {
            return buf;
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
        char *buf;
        const size_t s_;
        bool registered = false;
        uint64_t key_;
        void *desc = nullptr;
    };

}

#endif //NETWORKLAYER_UNIQUE_BUF_HH
