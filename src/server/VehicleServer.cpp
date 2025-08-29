#include "server/VehicleServer.h"
#include "common/Message.h"
#include "common/Logger.h"

#include <thread>
#include <utility>

namespace transport {

VehicleServer::VehicleServer() : ServerBase("VehicleServer") {}

bool VehicleServer::start(int port, const std::string& /*config_file*/) {
    port_ = port;

    // TLS server — identično kao kod CentralServer-a; ne diramo certifikate/ključeve
    tls_server_ = std::make_unique<TLSServer>();

    // Connection callback: ne blokiramo Asio nit, već svaku sesiju obrađujemo u posebnoj niti
    tls_server_->setConnectionCallback([this](std::unique_ptr<TLSSocket> client) {
        std::thread([this, c = std::move(client)]() mutable {
            handleClientMessage(std::move(c), nullptr);
        }).detach();
    });

    if (!tls_server_->start(port_, cert_file_, key_file_)) {
        logError("VehicleServer: failed to start TLS server on port " + std::to_string(port_));
        return false;
    }

    running_ = true;
    start_time_ = std::chrono::system_clock::now();
    logInfo("Vehicle Server started on port " + std::to_string(port_));
    return true;
}

void VehicleServer::handleClientMessage(std::unique_ptr<TLSSocket> client,
                                        std::unique_ptr<Message> /*message*/) {
    if (!client) return;

    total_connections_++;
    active_connections_++;

    logInfo("[VehicleServer] client connected from " + client->getPeerAddress()
            + ":" + std::to_string(client->getPeerPort()));

    // Ostavljen blokirajući tok
    while (running_ && client) {
        auto received = client->receiveMessage();   // blokirajuće čitanje poruke kroz TLS
        if (!received) break;
        logDebug("[VehicleServer] processing incoming message");
        processMessage(std::move(received), client);
    }

    active_connections_--;
    logInfo("[VehicleServer] client disconnected");
}

void VehicleServer::processMessage(std::unique_ptr<Message> /*message*/,
                                   std::unique_ptr<TLSSocket>& /*client*/) {

    logDebug("[VehicleServer] received message (stub)");
}

} // namespace transport

