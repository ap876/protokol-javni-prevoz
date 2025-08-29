#include "server/ServerBase.h"
#include <iostream>
#include <fstream>

namespace transport {

ServerBase::ServerBase(const std::string& server_name) 
    : server_name_(server_name) {
    logger_ = Logger::getLogger(server_name_);
    setupDefaultConfiguration();
}

ServerBase::~ServerBase() {
    stop();
}

void ServerBase::stop() {
    if (!running_) return;
    running_ = false;
    
    // Zaustavi TLS/Asio server
    if (tls_server_) {
        tls_server_->stop();
    }
    
    // Pričekaj accept nit 
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    
    // Pričekaj sve klijentske niti
    {
        std::lock_guard<std::mutex> lock(threads_mutex_);
        for (auto& thread : client_threads_) {
            if (thread && thread->joinable()) {
                thread->join();
            }
        }
        client_threads_.clear();
    }
    
    logInfo("Server stopped");
}

bool ServerBase::loadConfiguration(const std::string& config_file) {
    // Basic configuration loading (stub)
    std::ifstream file(config_file);
    if (!file.is_open()) {
        logWarning("Could not open configuration file: " + config_file);
        return false;
    }
    logInfo("Configuration loaded from: " + config_file);
    return true;
}

bool ServerBase::setCertificates(const std::string& cert_file, const std::string& key_file) {
    cert_file_ = cert_file;
    key_file_  = key_file;
    
    // Provjeri da fajlovi postoje 
    std::ifstream cert(cert_file_);
    std::ifstream key(key_file_);
    if (!cert.is_open()) {
        logError("Certificate file not found: " + cert_file_);
        return false;
    }
    if (!key.is_open()) {
        logError("Key file not found: " + key_file_);
        return false;
    }
    return true;
}

bool ServerBase::generateSelfSignedCertificate() {
    logWarning("generateSelfSignedCertificate() not implemented (skipping)");
    return false;
}

void ServerBase::logInfo(const std::string& message)    { if (logger_) logger_->info(message); }
void ServerBase::logWarning(const std::string& message) { if (logger_) logger_->warning(message); }
void ServerBase::logError(const std::string& message)   { if (logger_) logger_->error(message); }
void ServerBase::logDebug(const std::string& message)   { if (logger_) logger_->debug(message); }

void ServerBase::sendResponse(std::unique_ptr<TLSSocket>& client, std::unique_ptr<Message> response) {
    if (client && response) {
        client->sendMessage(*response);
    }
}

void ServerBase::sendErrorResponse(std::unique_ptr<TLSSocket>& client, const std::string& error, int code) {
    auto response = MessageFactory::createErrorResponse(error, code);
    sendResponse(client, std::move(response));
}

void ServerBase::sendSuccessResponse(std::unique_ptr<TLSSocket>& client, const std::string& message) {
    auto response = MessageFactory::createSuccessResponse(message);
    sendResponse(client, std::move(response));
}

void ServerBase::setupDefaultConfiguration() {
    start_time_ = std::chrono::system_clock::now();
}

std::string ServerBase::generateClientId() {
    static std::atomic<int> counter{0};
    return server_name_ + "_client_" + std::to_string(++counter);
}

void ServerBase::cleanupFinishedThreads() {
    std::lock_guard<std::mutex> lock(threads_mutex_);
    client_threads_.erase(
        std::remove_if(client_threads_.begin(), client_threads_.end(),
            [](const std::unique_ptr<std::thread>& /*t*/) {
              
                return false;
            }),
        client_threads_.end()
    );
}


// ------------------------ Boost.Asio TLS server kroz TLSServer ------------------------

void ServerBase::startServer() {
    // Kreiraj TLS server ako nije već kreiran
    if (!tls_server_) {
        tls_server_ = std::make_unique<TLSServer>();
    }

    // Postavi connection callback: svaku novu TLS konekciju obradi u zasebnoj niti,
    // kako accept/handshake (Asio) ne bi bili blokirani aplikativnim kodom.
    tls_server_->setConnectionCallback([this](std::unique_ptr<TLSSocket> client) {
        auto th = std::make_unique<std::thread>(
            [this, c = std::move(client)]() mutable {
                handleClient(std::move(c));
            }
        );
        {
            std::lock_guard<std::mutex> g(threads_mutex_);
            client_threads_.emplace_back(std::move(th));
        }
    });
)
    if (!tls_server_->start(port_, cert_file_, key_file_)) {
        logError("Failed to start TLSServer on port " + std::to_string(port_));
        throw std::runtime_error("TLSServer start failed");
    }
}

void ServerBase::acceptConnections() {
    // TLSServer intern o radi async_accept
    logDebug("acceptConnections(): TLSServer handles async accept internally");
}

void ServerBase::handleClient(std::unique_ptr<TLSSocket> client_socket) {
    // Delegiraj na aplikativni handler izvedene klase.
    // Poruka na startu nije poznata proslijedi nullptr kao drugi argument.
    handleClientMessage(std::move(client_socket), nullptr);
}

// ------------------------ Connection helpers (minimalni stubovi) ------------------------

bool ServerBase::validateClient(std::unique_ptr<TLSSocket>& /*client*/) {
    // Minimalna provjera na nivou baze klase (limit konekcija)
    if (active_connections_ >= max_connections_) {
        logWarning("Max connections reached");
        return false;
    }
    return true;
}

void ServerBase::disconnectClient(std::unique_ptr<TLSSocket>& client) {
    // Ako TLSSocket ima close/shutdown – pozovi; inače reset je dovoljan
    if (client) {
        // client->close(); // ako postoji
        client.reset();
    }
}

void ServerBase::broadcastMessage(std::unique_ptr<Message> /*message*/) {
    // Baza ne vodi listu živih TLSSocket-a; izvedene klase (npr. CentralServer) drže svoje pretplatnike.
    logDebug("broadcastMessage(): not implemented in ServerBase; use derived class");
}

// ------------------------ ServerConfig impl ------------------------

bool ServerConfig::loadFromFile(const std::string& config_file) {
    // Basic config file parsing (stub)
    std::ifstream file(config_file);
    if (!file.is_open()) {
        return false;
    }
    // TODO: parse key=value (nije predmet ove prepravke)
    return true;
}

bool ServerConfig::saveToFile(const std::string& config_file) const {
    std::ofstream file(config_file);
    if (!file.is_open()) {
        return false;
    }
    file << "port = " << port << "\n";
    file << "max_connections = " << max_connections << "\n";
    // Ostali parametri po potrebi...
    return true;
}

void ServerConfig::setDefaults() {
    port = 8080;
    max_connections = 100;
    connection_timeout = 300;
    require_authentication = true;
    enable_heartbeat = true;
    heartbeat_interval = 30;
    database_path = "transport.db";
    database_pool_size = 5;
    bind_address = "0.0.0.0";
    enable_ipv6 = false;
    socket_buffer_size = 65536;
    enable_tls = true;
    tls_handshake_timeout = 10;
}

bool ServerConfig::validate() const {
    if (port < 1 || port > 65535) return false;
    if (max_connections < 1) return false;
    if (connection_timeout < 1) return false;
    return true;
}

} // namespace transport

