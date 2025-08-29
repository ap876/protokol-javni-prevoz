#include "common/TLSServer.h"
#include "common/TLSSocket.h"

#include <iostream>

namespace transport {

using boost::asio::ip::tcp;

TLSServer::TLSServer() = default;
TLSServer::~TLSServer() { stop(); }

bool TLSServer::start(int port, const std::string& cert_file, const std::string& key_file) {
    if (running_) return true;
    running_ = true;

    try {
        // TLS kontekst (server)
        ssl_ctx_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);
        ssl_ctx_->set_options(
            boost::asio::ssl::context::default_workarounds
            | boost::asio::ssl::context::no_sslv2
            | boost::asio::ssl::context::single_dh_use);

        ssl_ctx_->use_certificate_chain_file(cert_file);
        ssl_ctx_->use_private_key_file(key_file, boost::asio::ssl::context::pem);

        // TCP acceptor
        acceptor_ = std::make_unique<tcp::acceptor>(
            io_, tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port)));
        acceptor_->set_option(boost::asio::socket_base::reuse_address(true));

        // Kreni prihvatati konekcije
        doAccept();

        // IO nit
        io_.restart();
        io_thread_ = std::make_unique<std::thread>([this]{
            try {
                io_.run();
            } catch (const std::exception& e) {
                std::cerr << "io_context error: " << e.what() << std::endl;
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "TLSServer start failed: " << e.what() << std::endl;
        running_ = false;
        return false;
    }
    return true;
}

void TLSServer::stop() {
    if (!running_) return;
    running_ = false;

    boost::system::error_code ec;
    if (acceptor_) acceptor_->close(ec);
    io_.stop();

    if (io_thread_ && io_thread_->joinable()) io_thread_->join();

    acceptor_.reset();
    ssl_ctx_.reset();
}

void TLSServer::doAccept() {
    if (!running_) return;

    auto raw_socket = std::make_shared<tcp::socket>(io_);
    acceptor_->async_accept(*raw_socket, [this, raw_socket](const boost::system::error_code& ec) {
        if (!running_) return;
        if (!ec) {
            // SSL stream nad prihvaćenim TCP socketom
            auto ssl_stream = std::make_shared<boost::asio::ssl::stream<tcp::socket>>(
                std::move(*raw_socket), *ssl_ctx_);

            // TLS handshake (server strana)
            ssl_stream->async_handshake(
                boost::asio::ssl::stream_base::server,
                [this, ssl_stream](const boost::system::error_code& hec) {
                    if (!running_) return;
                    if (!hec) {
                        // Pretvori u tvoj TLSSocket (server-side ctor)
                        auto clientSock = std::make_unique<TLSSocket>(ssl_stream);

                        if (on_connection_) {
                            // Pozovi korisnički callback u posebnoj niti da ne blokira accept petlju
                            std::thread([cb = on_connection_, c = std::move(clientSock)]() mutable {
                                cb(std::move(c));
                            }).detach();
                        }
                    } else {
                        std::cerr << "TLS handshake failed: " << hec.message() << std::endl;
                    }
                });
        } else {
            std::cerr << "Accept failed: " << ec.message() << std::endl;
        }

        // nastavi prihvat
        doAccept();
    });
}

} // namespace transport

