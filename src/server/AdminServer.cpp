#include "server/AdminServer.h"
#include "common/Message.h"
#include "common/Logger.h"

#include <thread>
#include <utility>
#include <chrono>

namespace transport {

AdminServer::AdminServer() : ServerBase("AdminServer") {}

bool AdminServer::start(int port, const std::string& /* config_file */) {
    port_ = port;

    // TLS server — koristi infrastrukturu na Boost.Asio (TLSServer/TLSSocket)
    tls_server_ = std::make_unique<TLSServer>();

    tls_server_->setConnectionCallback([this](std::unique_ptr<TLSSocket> client) {
        std::thread([this, c = std::move(client)]() mutable {
            handleClientMessage(std::move(c), nullptr);
        }).detach();
    });

    if (!tls_server_->start(port_, cert_file_, key_file_)) {
        logError("AdminServer: failed to start TLS server on port " + std::to_string(port_));
        return false;
    }

    running_ = true;
    start_time_ = std::chrono::system_clock::now();
    logInfo("Admin Server started on port " + std::to_string(port_));
    return true;
}

void AdminServer::handleClientMessage(std::unique_ptr<TLSSocket> client,
                                      std::unique_ptr<Message> /* message */) {
    if (!client) return;

    total_connections_++;
    active_connections_++;

    logInfo("[AdminServer] client connected from " + client->getPeerAddress()
            + ":" + std::to_string(client->getPeerPort()));

    // Ostavljen blokirajući tok 
    while (running_ && client) {
        auto received = client->receiveMessage();  // blokirajuće čitanje kroz TLS
        if (!received) break;
        logDebug("[AdminServer] processing incoming message");
        processMessage(std::move(received), client);
    }

    active_connections_--;
    logInfo("[AdminServer] client disconnected");
}

void AdminServer::processMessage(std::unique_ptr<Message> /* message */,
                                 std::unique_ptr<TLSSocket>& /* client */) {

    logDebug("[AdminServer] received message (stub)");
}

} // namespace transport

