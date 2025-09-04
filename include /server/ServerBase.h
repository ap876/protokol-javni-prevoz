#pragma once

#include "../common/TLSSocket.h"
#include "../common/TLSServer.h"

#include "../common/Message.h"
#include "../common/Logger.h"

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>

namespace transport {

// Base class for all server types
class ServerBase {
public:
    ServerBase(const std::string& server_name = "Server");
    virtual ~ServerBase();

    // Server lifecycle
    virtual bool start(int port, const std::string& config_file = "") = 0;
    virtual void stop();
    
    bool isRunning() const { return running_; }
    std::string getServerName() const { return server_name_; }
    int getPort() const { return port_; }

    // Configuration
    virtual bool loadConfiguration(const std::string& config_file);
    void setLogLevel(Logger::LogLevel level) { logger_->setLogLevel(level); }
    void setMaxConnections(int max_conn) { max_connections_ = max_conn; }
    void setConnectionTimeout(int timeout) { connection_timeout_ = timeout; }

    // Statistics
    int getActiveConnections() const { return active_connections_; }
    int getTotalConnections() const { return total_connections_; }
    std::chrono::system_clock::time_point getStartTime() const { return start_time_; }

    // Certificate management
    bool setCertificates(const std::string& cert_file, const std::string& key_file);
    bool generateSelfSignedCertificate(); // stub: ne diramo certifikate

protected:
    // Abstract methods to be implemented by derived classes
    virtual void handleClientMessage(std::unique_ptr<TLSSocket> client, 
                                     std::unique_ptr<Message> message) = 0;
    virtual void processMessage(std::unique_ptr<Message> message, 
                                std::unique_ptr<TLSSocket>& client) = 0;

    // Common server functionality (na Boost.Asio kroz TLSServer/TLSSocket)
    void startServer();                 // kreira TLSServer, postavlja callback i starta accept/TLS
    void acceptConnections();           // no-op (accept radi TLSServer interno)
    void handleClient(std::unique_ptr<TLSSocket> client_socket); // delegira na handleClientMessage
    
    // Logging
    void logInfo(const std::string& message);
    void logWarning(const std::string& message);
    void logError(const std::string& message);
    void logDebug(const std::string& message);

    // Connection management (minimalni stubovi — konkretne liste vode izvedene klase po potrebi)
    bool validateClient(std::unique_ptr<TLSSocket>& client);
    void disconnectClient(std::unique_ptr<TLSSocket>& client);
    void broadcastMessage(std::unique_ptr<Message> message);

    // Message utilities
    void sendResponse(std::unique_ptr<TLSSocket>& client, std::unique_ptr<Message> response);
    void sendErrorResponse(std::unique_ptr<TLSSocket>& client, const std::string& error, int code = -1);
    void sendSuccessResponse(std::unique_ptr<TLSSocket>& client, const std::string& message = "");

    // Server state
    std::atomic<bool> running_{false};
    std::atomic<int>  active_connections_{0};
    std::atomic<int>  total_connections_{0};
    
    std::string server_name_;
    int port_{0};
    std::chrono::system_clock::time_point start_time_{};

    // TLS configuration
    std::string cert_file_;
    std::string key_file_;
    std::unique_ptr<TLSServer> tls_server_;

    // Threading
    std::unique_ptr<std::thread> accept_thread_; 
    std::vector<std::unique_ptr<std::thread>> client_threads_;
    std::mutex threads_mutex_;

    // Configuration
    int  max_connections_{100};
    int  connection_timeout_{300}; // seconds
    bool require_authentication_{true};
    bool enable_heartbeat_{true};
    int  heartbeat_interval_{30}; // seconds

    // Logging
    std::shared_ptr<Logger> logger_;

    // Client management (meta-info; izvedene klase obično drže vlastite socket liste)
    struct ClientInfo {
        std::string client_id;
        std::string address;
        int port;
        std::chrono::system_clock::time_point connect_time;
        std::chrono::system_clock::time_point last_activity;
        bool authenticated{false};
    };
    
    std::vector<ClientInfo> connected_clients_;
    std::mutex clients_mutex_;

private:
    void cleanupFinishedThreads();
    void setupDefaultConfiguration();
    std::string generateClientId();
};

// Configuration structure for servers
struct ServerConfig {
    int port = 8080;
    int max_connections = 100;
    int connection_timeout = 300; // seconds
    bool require_authentication = true;
    bool enable_heartbeat = true;
    int heartbeat_interval = 30; // seconds
    std::string cert_file;
    std::string key_file;
    std::string log_file;
    Logger::LogLevel log_level = Logger::LogLevel::INFO;
    
    // Database configuration
    std::string database_path = "transport.db";
    int database_pool_size = 5;
    
    // Network configuration
    std::string bind_address = "0.0.0.0";
    bool enable_ipv6 = false;
    int socket_buffer_size = 65536;
    
    // Security configuration
    bool enable_tls = true;
    std::vector<std::string> allowed_cipher_suites;
    int tls_handshake_timeout = 10; // seconds
    
    bool loadFromFile(const std::string& config_file);
    bool saveToFile(const std::string& config_file) const;
    void setDefaults();
    bool validate() const;
};

} // namespace transport

