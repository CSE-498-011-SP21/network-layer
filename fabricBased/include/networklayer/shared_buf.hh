//
// Created by depaulsmiller on 3/25/21.
//

#ifndef NETWORKLAYER_SHARED_BUF_HH
#define NETWORKLAYER_SHARED_BUF_HH


#include <cstddef>
#include <cstring>
#include <rdma/fi_rma.h>
#include <cassert>
#include <string>
#include <atomic>

namespace cse498 {

    /**
     * Shared buffer; uses the semantics of shared_ptr<char[]>
     */
    class shared_buf {
    public:

        /**
         * Allocate buffer
         */
        shared_buf() : buf(new char[4096]), s_(4096), refCount(new std::atomic_uint32_t(1)) {
            registered = new std::atomic_bool(false);
            key_ = new std::atomic_uint64_t();
            desc = new std::atomic<void*>(nullptr);
        }

        /**
         * Allocate buffer of size s
         * @param s size
         */
        explicit shared_buf(size_t s) : buf(new char[s]), s_(s), refCount(new std::atomic_uint32_t(1)) {
            registered = new std::atomic_bool(false);
            key_ = new std::atomic_uint64_t();
            desc = new std::atomic<void*>(nullptr);
        }

        shared_buf(const shared_buf &other) {
            other.refCount->fetch_add(1);

            buf = other.buf;
            s_ = other.s_;
            registered = other.registered;
            key_ = other.key_;
            desc = other.desc;
            refCount = other.refCount;
        }

        shared_buf(shared_buf &&other) {
            buf = other.buf;
            other.buf = nullptr;
            s_ = other.s_;
            registered = other.registered;
            key_ = other.key_;
            desc = other.desc;
            other.desc = nullptr;
            refCount = other.refCount;
            other.refCount = nullptr;
        }

        ~shared_buf() {
            if (refCount && refCount->fetch_add(-1) == 1) {
                delete[] buf;
                delete refCount;
                delete registered;
                delete key_;
                delete desc;
            }
        }

        shared_buf &operator=(const shared_buf &other) {
            if (&other == this) {
                return *this;
            }

            if (refCount && refCount->fetch_add(-1) == 1) {
                delete[] buf;
                delete refCount;
                delete registered;
                delete key_;
                delete desc;
            }

            other.refCount->fetch_add(1);

            buf = other.buf;
            s_ = other.s_;
            registered = other.registered;
            key_ = other.key_;
            desc = other.desc;
            refCount = other.refCount;
            return *this;
        }

        shared_buf &operator=(shared_buf &&other) noexcept {
            if (&other == this) {
                return *this;
            }

            if (refCount && refCount->fetch_add(-1) == 1) {
                delete[] buf;
                delete refCount;
                delete registered;
                delete key_;
                delete desc;
            }

            buf = other.buf;
            other.buf = nullptr;
            s_ = other.s_;
            registered = other.registered;
            key_ = other.key_;
            desc = other.desc;
            other.desc = nullptr;
            refCount = other.refCount;
            other.refCount = nullptr;
            return *this;
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
        inline shared_buf &operator=(const std::string &s) {
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
            registered->store(true);
            key_->store(key);
            desc->store(d);
        }

        /**
         * Key associated with buffer
         * @return
         */
        [[nodiscard]] inline uint64_t key() const {
            assert(registered);
            return key_->load();
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
            return desc->load();
        }

        /**
         * Returns if the buffer has been registered
         * @return
         */
        [[nodiscard]] inline bool isRegistered() const {
            return registered->load();
        }

    private:
        char *buf;
        size_t s_;
        std::atomic_bool *registered;
        std::atomic_uint64_t *key_;
        std::atomic<void *> *desc = nullptr;
        std::atomic_uint32_t *refCount;
    };

}


#endif //NETWORKLAYER_SHARED_BUF_HH
