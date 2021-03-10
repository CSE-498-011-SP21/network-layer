#include <atomic>

#ifndef NETWORKLAYER_SM_HH
#define NETWORKLAYER_SM_HH

namespace cse498 {

    template<typename T>
    class SharedMemory {
    public:
        SharedMemory() {}
        virtual ~SharedMemory() {};

        /**
         * Atomically read data
         * @return
         */
        virtual T load() = 0;

        /**
         * Atomically store data.
         * @param t to store
         */
        virtual void store(T t) = 0;

        /**
         * Compare and swap shared memory
         * @param expected expected value
         * @param desired desired value
         * @return value read
         */
        virtual T compare_and_swap(T expected, T desired) = 0;
    };

    template<typename T>
    class LocalSharedMemory final: public SharedMemory<T> {
    public:
        LocalSharedMemory() {}

        ~LocalSharedMemory() {}

        T load() {
            return data.load();
        }

        void store(T t) {
            data.store(t);
        }

        T compare_and_swap(T expected, T desired) {
            T e = expected;
            data.compare_exchange_strong(e, desired);
            return e;
        }

    private:
        std::atomic<T> data;
    };

}

#endif //NETWORKLAYER_SM_HH
