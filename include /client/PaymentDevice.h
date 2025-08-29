#pragma once

#include "ClientBase.h"
#include "common/Message.h"
#include "common/Logger.h"
#include "common/TLSSocket.h"

#include <string>
#include <memory>
#include <thread>
#include <atomic>

namespace transport {

class PaymentDevice : public ClientBase {
public:
    PaymentDevice();
    ~PaymentDevice() = default;
    
    // Uspostavlja TLS konekciju preko TLSSocket (Boost.Asio ispod haube)
    bool connect(const std::string& server, int port) override;
    void disconnect() override;

protected:
    // Aplikativna obrada primljenih poruka
    void handleMessage(std::unique_ptr<Message> message) override;

private:
    // RX petlja (blokirajuće čitanje poruka u posebnoj niti)
    void receiveLoop_();

    std::string device_uri_;
    std::string vehicle_type_;

    // TLS kanal (Boost.Asio ispod TLSSocket-a)
    std::unique_ptr<TLSSocket> socket_;

    // Reader nit i flag
    std::unique_ptr<std::thread> rx_thread_;
    std::atomic<bool> running_{false};
};

} // namespace transport

