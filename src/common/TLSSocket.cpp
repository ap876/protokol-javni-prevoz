#include "common/TLSSocket.h"
#include "common/Message.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>

namespace transport {

using boost::asio::ip::tcp;

// ===================== interna Asio struktura =====================
struct TLSSocket::AsioState {
    boost::asio::io_context io;
    std::unique_ptr<boost::asio::ssl::context> ssl_ctx; // kreiramo po potrebi
    std::shared_ptr<boost::asio::ssl::stream<tcp::socket>> stream; // dijeljeno zbog server ctor-a
    bool running = false;
};

// ===================== konstruktori / destruktor ==================
TLSSocket::TLSSocket(Mode mode)
    : mode_(mode) {
    asio_ = std::make_shared<AsioState>();
}

TLSSocket::TLSSocket(std::shared_ptr<ssl_stream_t> accepted_stream)
    : mode_(Mode::SERVER) {
    asio_ = std::make_shared<AsioState>();
    asio_->stream = std::move(accepted_stream);
    connected_ = true;
    tls_established_ = true;
}

TLSSocket::~TLSSocket() {
    disconnect();
    cleanupSSL(); // no-op u Asio varijanti
}

// ===================== “OpenSSL compat” no-op =====================
bool TLSSocket::initializeSSL() { return true; }
void TLSSocket::cleanupSSL() {}

// ===================== POSIX server API (nije podržan ovdje) ======
bool TLSSocket::createSocket() { return false; }
bool TLSSocket::setSocketOptions() { return true; }

bool TLSSocket::bind(int) {
    setLastError("TLSSocket::bind is not supported in Asio mode. Use TLSServer.");
    return false;
}
bool TLSSocket::listen(int) {
    setLastError("TLSSocket::listen is not supported in Asio mode. Use TLSServer.");
    return false;
}
std::unique_ptr<TLSSocket> TLSSocket::accept() {
    setLastError("TLSSocket::accept is not supported in Asio mode. Use TLSServer.");
    return nullptr;
}
void TLSSocket::closeSocket() {
    // Cleanup ide kroz disconnect(); nema raw fd-a ovdje
}

// ===================== TLS config (putanje) =======================
bool TLSSocket::loadCertificate(const std::string& cert_file, const std::string& key_file) {
    cert_file_ = cert_file;
    key_file_  = key_file;
    return true;
}
bool TLSSocket::loadCACertificate(const std::string& ca_file) {
    ca_file_ = ca_file;
    return true;
}

// ===================== CLIENT CONNECT (Boost.Asio TLS) ============
bool TLSSocket::connect(const std::string& hostname, int port) {
    if (mode_ != Mode::CLIENT) {
        setLastError("Connect only available in client mode");
        return false;
    }

    try {
        // 1) ssl context
        asio_->ssl_ctx = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
        asio_->ssl_ctx->set_options(
            boost::asio::ssl::context::default_workarounds
            | boost::asio::ssl::context::no_sslv2
            | boost::asio::ssl::context::single_dh_use);

        if (!cert_file_.empty() && !key_file_.empty()) {
            asio_->ssl_ctx->use_certificate_chain_file(cert_file_);
            asio_->ssl_ctx->use_private_key_file(key_file_, boost::asio::ssl::context::pem);
        }
        if (!ca_file_.empty()) {
            asio_->ssl_ctx->load_verify_file(ca_file_);
            asio_->ssl_ctx->set_verify_mode(boost::asio::ssl::verify_peer);
        } else {
            asio_->ssl_ctx->set_verify_mode(boost::asio::ssl::verify_none);
        }

        // 2) socket + resolve + connect
        tcp::resolver resolver(asio_->io);
        auto endpoints = resolver.resolve(hostname, std::to_string(port));
        asio_->stream = std::make_shared<boost::asio::ssl::stream<tcp::socket>>(asio_->io, *asio_->ssl_ctx);

        boost::asio::connect(asio_->stream->lowest_layer(), endpoints);

        // 3) TLS handshake
        asio_->stream->handshake(boost::asio::ssl::stream_base::client);

        connected_ = true;
        tls_established_ = true;
        return true;

    } catch (const std::exception& e) {
        setLastError(std::string("Asio TLS connect failed: ") + e.what());
        connected_ = false;
        tls_established_ = false;
        return false;
    }
}

// ===================== setupTLS / performTLSHandshake =============
bool TLSSocket::setupTLS()            { return true; }
bool TLSSocket::performTLSHandshake() { return true; }

// ===================== Sync send/recv =============================
ssize_t TLSSocket::send(const void* data, size_t length) {
    if (!tls_established_ || !asio_ || !asio_->stream) {
        setLastError("TLS not established");
        return -1;
    }
    try {
        size_t total = 0;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        while (total < length) {
            total += boost::asio::write(*asio_->stream, boost::asio::buffer(p + total, length - total));
        }
        return static_cast<ssize_t>(total);
    } catch (const std::exception& e) {
        setLastError(std::string("Asio TLS write failed: ") + e.what());
        return -1;
    }
}

ssize_t TLSSocket::receive(void* buffer, size_t length) {
    if (!tls_established_ || !asio_ || !asio_->stream) {
        setLastError("TLS not established");
        return -1;
    }
    try {
        size_t total = 0;
        uint8_t* p = static_cast<uint8_t*>(buffer);
        while (total < length) {
            total += boost::asio::read(*asio_->stream, boost::asio::buffer(p + total, length - total));
        }
        return static_cast<ssize_t>(total);
    } catch (const std::exception& e) {
        setLastError(std::string("Asio TLS read failed: ") + e.what());
        return -1;
    }
}

bool TLSSocket::sendMessage(const Message& message) {
    if (!tls_established_) {
        setLastError("TLS not established");
        return false;
    }
    auto bytes = message.serialize(); // [Header][Payload]
    return send(bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
}

std::unique_ptr<Message> TLSSocket::receiveMessage() {
    if (!tls_established_) {
        setLastError("TLS not established");
        return nullptr;
    }

    // 1) header
    std::vector<uint8_t> header_bytes(sizeof(Message::Header));
    if (receive(header_bytes.data(), header_bytes.size()) != static_cast<ssize_t>(header_bytes.size())) {
        return nullptr;
    }

    // 2) doznaj payload dužinu
    Message::Header hdr{};
    std::memcpy(&hdr, header_bytes.data(), sizeof(Message::Header));
    hdr.magic       = ntohl(hdr.magic);
    hdr.version     = ntohs(hdr.version);
    hdr.type        = static_cast<MessageType>(ntohs(static_cast<uint16_t>(hdr.type)));
    hdr.length      = ntohl(hdr.length);
    hdr.sequence_id = ntohl(hdr.sequence_id);
    hdr.session_id  = ntohl(hdr.session_id);
    hdr.checksum    = ntohl(hdr.checksum);

    if (hdr.magic != 0x54504D50) {
        setLastError("Invalid message magic");
        return nullptr;
    }

    // 3) payload
    std::vector<uint8_t> payload(hdr.length);
    if (hdr.length > 0) {
        if (receive(payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
            return nullptr;
        }
    }

    // 4) sklopi [Header][Payload] i deserijalizuj
    std::vector<uint8_t> full;
    full.reserve(header_bytes.size() + payload.size());
    full.insert(full.end(), header_bytes.begin(), header_bytes.end());
    full.insert(full.end(), payload.begin(), payload.end());

    auto msg = std::make_unique<Message>();
    if (!msg->deserialize(full)) {
        setLastError("Failed to deserialize message");
        return nullptr;
    }
    return msg;
}

// ===================== Disconnect / info ==========================
void TLSSocket::disconnect() {
    async_running_ = false;
    try {
        if (asio_ && asio_->stream) {
            boost::system::error_code ec;
            asio_->stream->shutdown(ec);
            asio_->stream->lowest_layer().close(ec);
        }
    } catch (...) {}
    connected_ = false;
    tls_established_ = false;
}

std::string TLSSocket::getPeerAddress() const {
    try {
        if (asio_ && asio_->stream)
            return asio_->stream->lowest_layer().remote_endpoint().address().to_string();
    } catch (...) {}
    return "unknown";
}

int TLSSocket::getPeerPort() const {
    try {
        if (asio_ && asio_->stream)
            return static_cast<int>(asio_->stream->lowest_layer().remote_endpoint().port());
    } catch (...) {}
    return 0;
}

std::string TLSSocket::getLocalAddress() const {
    try {
        if (asio_ && asio_->stream)
            return asio_->stream->lowest_layer().local_endpoint().address().to_string();
    } catch (...) {}
    return "unknown";
}

int TLSSocket::getLocalPort() const {
    try {
        if (asio_ && asio_->stream)
            return static_cast<int>(asio_->stream->lowest_layer().local_endpoint().port());
    } catch (...) {}
    return 0;
}

// ===================== Async API (stub) ===========================
bool TLSSocket::startAsyncReceive() {
    setLastError("Async receive not implemented in this minimal Asio port");
    return false;
}
void TLSSocket::stopAsyncReceive() {}

int TLSSocket::getSocketError() const { return 0; }

// ===================== helperi za greške ==========================
void TLSSocket::setLastError(const std::string& e) { last_error_ = e; }
std::string TLSSocket::getSSLError() const { return "asio"; }
void TLSSocket::logSSLError(const std::string&) const {}

} // namespace transport

