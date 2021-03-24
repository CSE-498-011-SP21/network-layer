//
// Created by depaulsmiller on 3/24/21.
//

#ifndef NETWORKLAYER_UNIQUE_BUF_HH
#define NETWORKLAYER_UNIQUE_BUF_HH

#include <cstddef>
#include <cstring>
#include <rdma/fi_rma.h>
#include <cassert>
#include <string>

namespace cse498 {

    class unique_buf {
    public:
        unique_buf() : buf(new char[4096]) {}

        unique_buf(const unique_buf&) = delete;

        unique_buf(unique_buf&& other) = default;

        ~unique_buf() {
            delete[] buf;
        }

        inline char &operator[](size_t idx) {
            return buf[idx];
        }

        inline const char &operator[](size_t idx) const {
            return buf[idx];
        }

        inline void cpyTo(const char *input, size_t s, size_t offset = 0) {
            memcpy(buf + offset, input, s);
        }

        inline void cpyFrom(char *output, size_t s, size_t offset = 0) const {
            memcpy(output, buf + offset, s);
        }

        inline unique_buf& operator=(const std::string& s){
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

        inline void registerMemoryCallback(uint64_t key, void* d) {
            registered = true;
            key_ = key;
            desc = d;
        }

        [[nodiscard]] inline uint64_t key() const {
            assert(registered);
            return key_;
        }

        [[nodiscard]] inline size_t size() const {
            return s_;
        }

        [[nodiscard]] inline void* getDesc() {
            return desc;
        }

        [[nodiscard]] inline bool isRegistered() const{
            return registered;
        }

    private:
        char *buf;
        const size_t s_ = 4096;
        bool registered = false;
        uint64_t key_;
        void* desc = nullptr;
    };

}

#endif //NETWORKLAYER_UNIQUE_BUF_HH
