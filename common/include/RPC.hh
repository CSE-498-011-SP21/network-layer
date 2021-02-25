#include <functional>
#include <iostream>
#include <vector>

#ifndef NETWORKLAYER_RPC_HH
#define NETWORKLAYER_RPC_HH

namespace cse498 {

    /**
     * Pack_t type is used as the basic data type for serialization.
     */
    using pack_t = std::vector<char>;

    /**
     * RPC server class.
     */
    class RPC {
    public:
        /**
         * Constructor
         */
        RPC() {}

        /**
         * Destructor
         */
        virtual ~RPC() {}

        /**
         * Register RPC with an id number and a function taking in pack_t and returning a pack_t
         * @param fnId id number
         * @param fn RPC function
         */
        virtual void registerRPC(uint64_t fnId, std::function<pack_t(pack_t)> fn) = 0;
    };

    /**
     * RPC client class.
     */
    class RPClient {
    public:
        /**
         * Constructor
         */
        RPClient() {}

        /**
         * Destructor
         */
        virtual ~RPClient() {}

        /**
         * Call remote function by sending data
         * @param fnID RPC id number
         * @param data data to send
         * @return pack_t returned by remote function
         */
        virtual pack_t callRemote(uint64_t fnID, pack_t data) = 0;
    };

}

#endif //NETWORKLAYER_RPC_HH
