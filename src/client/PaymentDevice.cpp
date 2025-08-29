#include "client/PaymentDevice.h"
#include "common/Message.h"
#include "common/Logger.h"

#include <utility>
#include <chrono>

namespace transport {

PaymentDevice::PaymentDevice() : ClientBase("PaymentDevice") {}

bool PaymentDevice::connect(const std::string& server, int port) {
    logInfo("Connecting to server: " + server + ":" + std::to_string(port));

    socket_ = std::make_unique<TLSSocket>(TLSSocket::Mode::CLIENT);

    

    if (!socket_->connect(server, port)) {
        logError("Failed to connect: " + socket_->getLastError());
        return false;
    }
    logInfo("Connected");
    return true;
}


void PaymentDevice::disconnect() {
    logInfo("Disconnecting from server");
    running_ = false;

    
    if (rx_thread_ && rx_thread_->joinable()) {
        rx_thread_->join();
    }
    rx_thread_.reset();

    socket_.reset();
    logInfo("Disconnected");
}

void PaymentDevice::receiveLoop_() {
    // Blokirajuće čitanje poruka; asinhronost se postiže time što RX ide u zasebnoj niti,
    // dok glavni tok može paralelno da šalje poruke (sync/async kombinacija za uvjet zadatka).
    while (running_) {
        if (!socket_) break;

        auto msg = socket_->receiveMessage(); // blokirajuće čitanje jedne uokvirene poruke
        if (!msg) {
            // peer zatvorio ili greška
            if (running_) logError("Receive failed or connection closed by peer");
            break;
        }
        handleMessage(std::move(msg));
    }
}

void PaymentDevice::handleMessage(std::unique_ptr<Message> /*message*/) {
    // Obradi payment-device specifične poruke
    // Npr.: potvrda naplate, zahtjev za PIN, receipt itd.
    // logInfo("PaymentDevice: received message");
}

} // namespace transport

