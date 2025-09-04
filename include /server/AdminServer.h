#pragma once

#include "ServerBase.h"
#include "../common/TLSSocket.h"
#include <memory>

namespace transport {

class AdminServer : public ServerBase {
public:
    AdminServer();
    
    // Pokreće TLS/Asio accept petlju (cert/key ostaju iz ServerBase)
    bool start(int port, const std::string& config_file = "") override;

protected:
    // Obrada jedne TLS konekcije (poziva se iz connection callback-a)
    void handleClientMessage(std::unique_ptr<TLSSocket> client,
                             std::unique_ptr<Message> message) override;

    // Aplikativna obrada poruke (stub/log dok ne dodamo tipove poruka)
    void processMessage(std::unique_ptr<Message> message,
                        std::unique_ptr<TLSSocket>& client) override;
};

} // namespace transport

