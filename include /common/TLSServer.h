#pragma once
#include <memory>
#include <thread>
#include <functional>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "TLSSocket.h"

namespace transport {

class TLSServer {
public:
    using ConnectionCallback = std::function<void(std::unique_ptr<TLSSocket>)>;

    TLSServer();
    ~TLSServer();

    // Pokreće TLS server na portu i učitava cert/key
    bool start(int port, const std::string& cert_file, const std::string& key_file);

    // Zaustavlja accept petlju i gasi io_context
    void stop();

    // Callback za svaku novu konekciju (predaje se kao gotov TLSSocket)
    void setConnectionCallback(ConnectionCallback cb) { on_connection_ = std::move(cb); }

private:
    void doAccept();

    // Non-copyable
    TLSServer(const TLSServer&) = delete;
    TLSServer& operator=(const TLSServer&) = delete;

    boost::asio::io_context io_;
    std::unique_ptr<std::thread> io_thread_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::unique_ptr<boost::asio::ssl::context> ssl_ctx_;
    std::atomic<bool> running_{false};
    ConnectionCallback on_connection_;
};

} // namespace transport

