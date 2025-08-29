#pragma once

#include "common/TLSSocket.h"
#include "common/Message.h"
#include "common/Logger.h"

#include <string>
#include <memory>
#include <vector>
#include <sstream>
#include <optional>

namespace transport {

class UserInterface {
public:
    UserInterface();
    
    // Ako je 'server' == "auto", pokuša UDP multicast DISCOVER pa se spoji na pronađeni server.
    bool connect(const std::string& server, int port, const std::string& ca_file = "");
    bool authenticate(const std::string& urn);
    void startInteractiveSession();
    void setLogLevel(Logger::LogLevel level);

private:
    std::unique_ptr<TLSSocket> socket_;
    std::shared_ptr<Logger> logger_;
    std::string session_token_;
    std::string current_urn_;
    bool authenticated_{false};
    
    // help + postojeći handleri
    void showHelp();
    void handleRegister(const std::string& input);
    void handleAuthenticate(const std::string& input);
    void handleRegisterDevice(const std::string& input);
    void handleReserve(const std::string& input);
    void handlePurchase(const std::string& input);
    void handleCreateGroup(const std::string& input);
    void handleListen(const std::string& input);

    // NOVO: komande za članove grupe (lider)
    void handleAddMember(const std::string& input);
    void handleRemoveMember(const std::string& input);
    
    // util
    std::vector<std::string> splitString(const std::string& str, char delimiter);

    // --- UDP multicast DISCOVER (opcionalno) ---
    // Vrati host i port centralnog servera ako je pronađen u zadanom timeout-u.
    std::optional<std::pair<std::string,int>> discoverServer(int timeout_ms = 1500);
};

} // namespace transport

