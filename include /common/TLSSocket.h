#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace transport {

class Message;

class TLSSocket {
public:
    enum class Mode { CLIENT, SERVER };

    TLSSocket(Mode mode = Mode::CLIENT);
    using ssl_stream_t = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    explicit TLSSocket(std::shared_ptr<ssl_stream_t> accepted_stream); // za server-side

    ~TLSSocket();

    // Connection
    bool connect(const std::string& hostname, int port);
    bool bind(int port);                 // not supported in Asio mode (koristi TLSServer)
    bool listen(int backlog = 5);        // not supported
    std::unique_ptr<TLSSocket> accept(); // not supported
    void disconnect();
    
    bool isConnected() const { return connected_; }
    bool isTLSEstablished() const { return tls_established_; }

    // TLS config (putanje se primjene pri connect/konstrukciji)
    bool loadCertificate(const std::string& cert_file, const std::string& key_file);
    bool loadCACertificate(const std::string& ca_file);
    bool setupTLS();            // no-op
    bool performTLSHandshake(); // no-op

    // Sync I/O
    bool sendMessage(const Message& message);
    std::unique_ptr<Message> receiveMessage();
    ssize_t send(const void* data, size_t length);
    ssize_t receive(void* buffer, size_t length);

    // Async API (nije implementirano u minimal portu)
    using MessageCallback = std::function<void(std::unique_ptr<Message>)>;
    using ErrorCallback   = std::function<void(const std::string&)>;
    void setMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb)     { error_callback_   = std::move(cb); }
    bool startAsyncReceive();
    void stopAsyncReceive();

    // Stream helpers
    bool sendStream(const std::vector<uint8_t>& data) { return send(data.data(), data.size()) == (ssize_t)data.size(); }
    std::vector<uint8_t> receiveStream(size_t max_length = 8192) {
        std::vector<uint8_t> buf(max_length);
        ssize_t n = receive(buf.data(), max_length);
        if (n < 0) return {};
        buf.resize((size_t)n);
        return buf;
    }
    
    // Info
    std::string getPeerAddress() const;
    int getPeerPort() const;
    std::string getLocalAddress() const;
    int getLocalPort() const;

    // Errors
    std::string getLastError() const { return last_error_; }
    int getSocketError() const;

private:
    // Originalno stanje/flagovi
    Mode mode_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> tls_established_{false};
    std::atomic<bool> async_running_{false};
    std::string last_error_;
    std::string cert_file_, key_file_, ca_file_;

    // Async thread holderi (API kompatibilnost)
    std::unique_ptr<std::thread> receive_thread_;
    std::mutex receive_mutex_;
    std::condition_variable receive_cv_;
    MessageCallback message_callback_;
    ErrorCallback   error_callback_;

    // Boost.Asio stanje
    struct AsioState;
    std::shared_ptr<AsioState> asio_;

    // Helperi (samo deklaracije – definicija u .cpp)
    bool initializeSSL();
    void cleanupSSL();
    bool createSocket();
    void closeSocket();
    void asyncReceiveLoop();
    void setLastError(const std::string& error);
    bool setSocketOptions();
    std::string getSSLError() const;
    void logSSLError(const std::string& operation) const;

    TLSSocket(const TLSSocket&) = delete;
    TLSSocket& operator=(const TLSSocket&) = delete;
};

} // namespace transport

