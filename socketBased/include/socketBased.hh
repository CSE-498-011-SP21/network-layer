//
// Created by depaulsmiller on 2/18/21.
//

#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <networklayer/RPC.hh>
#include <tbb/concurrent_unordered_map.h>
#include <cassert>

namespace cse498 {

    int DEFAULT_PORT = 8080;

    struct Header {
        uint64_t fnID;
        uint64_t sizeOfArg;
    };

    class ServerConnection : public std::enable_shared_from_this<ServerConnection> {
    public:
        static std::shared_ptr<ServerConnection> Create(boost::asio::io_service &io_service,
                                                        tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(
                                                                pack_t)>> *fnMap) {
            return std::shared_ptr<ServerConnection>(new ServerConnection(io_service, fnMap));
        }

        void Start() {
            auto shared_this = shared_from_this();
            auto sharedStreamBuf = std::make_shared<boost::asio::streambuf>();
            auto handler = [shared_this, sharedStreamBuf](const boost::system::error_code &error,
                                                          size_t bytes_transferred) {
                std::cerr << "Buffer size " << sharedStreamBuf->size() << std::endl;
                std::cerr << "Buffer capacity " << sharedStreamBuf->capacity() << std::endl;

                if(error == boost::asio::error::eof){
                    return;
                }

                shared_this->HandleReadSize(error, bytes_transferred, sharedStreamBuf);

            };
            boost::asio::async_read(socket_, *sharedStreamBuf, boost::asio::transfer_exactly(sizeof(Header)),
                                    handler);

        }

        boost::asio::ip::tcp::socket &getSocket() {
            return socket_;
        }

    private:
        ServerConnection(boost::asio::io_service &io_service,
                         tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(pack_t)>> *fnMap
        ) : socket_(io_service), fnMap_(fnMap) {

        }

        void HandleReadSize(const boost::system::error_code &error, size_t bytes_transferred,
                            std::shared_ptr<boost::asio::streambuf> sharedStreamBuf) {
            std::cerr << "SERVER: Transferred " << bytes_transferred << " bytes" << std::endl;

            auto data = sharedStreamBuf->data();
            Header header = *reinterpret_cast<const Header *>(&*boost::asio::buffers_begin(data));
            sharedStreamBuf->consume(bytes_transferred); // get rid of the read in bytes

            std::cerr << "SERVER: Read header " << header.fnID << " fn with size " << header.sizeOfArg << std::endl;

            auto shared_this = shared_from_this();
            auto handler = [shared_this, sharedStreamBuf, header](const boost::system::error_code &error,
                                                                  size_t bytes_transferred) {
                shared_this->HandleCall(error, bytes_transferred, sharedStreamBuf, header);
            };
            boost::asio::async_read(socket_, *sharedStreamBuf, boost::asio::transfer_exactly(header.sizeOfArg),
                                    handler);


        }

        void HandleCall(const boost::system::error_code &error, size_t bytes_transferred,
                        std::shared_ptr<boost::asio::streambuf> sharedStreamBuf, const Header &header) {
            std::cerr << "SERVER: Transferred " << bytes_transferred << " bytes" << std::endl;

            std::cerr << "SERVER: Read args of size " << header.sizeOfArg << std::endl;

            auto data = sharedStreamBuf->data();
            const char *arg = reinterpret_cast<const char *>(&*boost::asio::buffers_begin(data));
            std::vector<char> v(header.sizeOfArg);
            mempcpy(v.data(), arg, header.sizeOfArg);
            auto res = fnMap_->find(header.fnID);
            auto fn = res->second;
            auto v2 = fn(v);

            uint64_t newS = v2.size();

            std::string resString = std::string(reinterpret_cast<const char *>(&newS), sizeof(uint64_t));

            auto buffer_ptr = std::make_shared<std::string>();

            *buffer_ptr = resString + std::string(v2.begin(), v2.end());

            auto buf = boost::asio::buffer(buffer_ptr->data(), buffer_ptr->size());

            auto shared_this = shared_from_this();
            auto write_handler = [shared_this, buffer_ptr](const boost::system::error_code &error,
                                                           size_t bytes_transferred) {
                shared_this->HandleResponse(error, bytes_transferred);
            };
            boost::asio::async_write(socket_, buf, write_handler);
        }

        void HandleResponse(const boost::system::error_code &error, size_t bytes_transferred) {
            std::cerr << "SERVER: Transferred " << bytes_transferred << " bytes" << std::endl;
            if (error)
                std::cerr << error.message() << std::endl;
            Start();
        }

        boost::asio::ip::tcp::socket socket_;
        tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(pack_t)>> *fnMap_;
    };

    class SocketRPC final : public RPC {
        using tcp = boost::asio::ip::tcp;
    public:
        SocketRPC() : fnMap(new tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(pack_t)>>()), io_service(),
                      acceptor_(io_service, tcp::endpoint(boost::asio::ip::tcp::v4(), DEFAULT_PORT)) {
            t = std::thread([this](){
               io_service.run();
            });
            StartAccept();
        }

        ~SocketRPC() {
            io_service.stop();
            t.join();
            delete fnMap;
        }

        /**
         * Register RPC with an id number and a function taking in pack_t and returning a pack_t
         * @param fnID id number
         * @param fn RPC function
         */
        void registerRPC(uint64_t fnID, std::function<pack_t(pack_t)> fn) {
            assert(fnMap->insert({fnID, fn}).second);
        }

    private:

        void StartAccept() {
            auto conn = ServerConnection::Create(io_service, fnMap);
            tcp::socket &socket = conn->getSocket();
            auto handler = [this, conn](const boost::system::error_code &error) {
                HandleAccept(conn, error);
            };
            acceptor_.async_accept(socket, handler);
        }

        void HandleAccept(std::shared_ptr<ServerConnection> conn, const boost::system::error_code &error) {
            if (error)
                throw boost::system::system_error(error);
            conn->Start();
            StartAccept();
        }

        tbb::concurrent_unordered_map<uint64_t, std::function<pack_t(pack_t)>> *fnMap;
        boost::asio::io_service io_service;
        tcp::acceptor acceptor_;
        std::thread t;
    };

    class SocketRPClient final : public RPClient {
        using tcp = boost::asio::ip::tcp;
    public:
        SocketRPClient(const std::string &address, uint16_t port) : socket_(io_service) {
            socket_.connect(tcp::endpoint(boost::asio::ip::address::from_string(address), port));

            std::cerr << "Connected\n";

        }

        ~SocketRPClient() {
            socket_.close();
        }

        /**
         * Call remote function by sending data
         * @param fnID RPC id number
         * @param data data to send
         * @return pack_t returned by remote function
         */
        pack_t callRemote(uint64_t fnID, pack_t data) {
            boost::system::error_code error;

            Header h;
            h.fnID = fnID;
            h.sizeOfArg = data.size();

            boost::asio::write(socket_, boost::asio::buffer(&h, sizeof(Header)), error);
            if (error) {
                std::cerr << __FILE__ << ":" << __LINE__ << " send failed : " << error.message() << std::endl;
                _exit(1);
            }

            std::cerr << "CLIENT: Written header " << h.fnID << " fn with size " << h.sizeOfArg << std::endl;

            boost::asio::write(socket_, boost::asio::buffer(data), error);
            if (error) {
                std::cerr << __FILE__ << ":" << __LINE__ << " send failed : " << error.message() << std::endl;
                _exit(1);
            }

            std::cerr << "CLIENT: Written data with size " << h.sizeOfArg << std::endl;

            boost::asio::streambuf receive_buffer;
            boost::asio::read(socket_, receive_buffer, boost::asio::transfer_exactly(sizeof(uint64_t)), error);

            auto sizeTransfer = *boost::asio::buffer_cast<const size_t*>(receive_buffer.data());

            receive_buffer.consume(sizeTransfer);

            std::cerr << "CLIENT: Read result header size of " << sizeTransfer << std::endl;

            boost::asio::read(socket_, receive_buffer, boost::asio::transfer_exactly(sizeTransfer), error);

            std::cerr << "CLIENT: read result of size " << sizeTransfer << std::endl;

            const char* underlyingdata = boost::asio::buffer_cast<const char*>(receive_buffer.data());

            pack_t p(sizeTransfer);

            mempcpy(p.data(), underlyingdata, sizeTransfer);

            return p;
        }

    private:
        boost::asio::io_service io_service;
        tcp::socket socket_;

    };
}