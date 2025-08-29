#include "client/UserInterface.h"
#include "client/ClientBase.h"
#include "common/TLSSocket.h"
#include "common/Message.h"
#include "common/Logger.h"

#include <iostream>
#include <string>
#include <sstream>
#include <istream>
#include <chrono>

// Boost.Asio 
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>

namespace transport {

namespace {
constexpr const char* MCAST_ADDR = "239.192.0.1";
constexpr unsigned short MCAST_PORT = 30001;
} // namespace

UserInterface::UserInterface() {
    logger_ = Logger::getLogger("UserInterface");
}

bool UserInterface::connect(const std::string& server, int port, const std::string& ca_file) {
    std::string target_host = server;
    int         target_port = port;

   
    if (server == "auto") {
        if (auto found = discoverServer(/*timeout_ms*/1500)) {
            target_host = found->first;
            target_port = found->second;
            logger_->info("Discovered central server via multicast: " + target_host + ":" + std::to_string(target_port));
        } else {
            logger_->warning("Multicast discover timed out; falling back to provided port " + std::to_string(port));
            target_host = "127.0.0.1";
        }
    }

    socket_ = std::make_unique<TLSSocket>(TLSSocket::Mode::CLIENT);

    if (!ca_file.empty()) {
        socket_->loadCACertificate(ca_file);
    }

    if (!socket_->connect(target_host, target_port)) {
        logger_->error("Failed to connect to server: " + socket_->getLastError());
        return false;
    }

    logger_->info("Connected to server successfully");
    return true;
}

bool UserInterface::authenticate(const std::string& urn) {
    if (!socket_) {
        logger_->error("Not connected to server");
        return false;
    }

    auto auth_message = MessageFactory::createAuthRequest(urn);
    if (!socket_->sendMessage(*auth_message)) {
        logger_->error("Failed to send authentication request");
        return false;
    }

    auto response = socket_->receiveMessage();
    if (!response) {
        logger_->error("Failed to receive authentication response");
        return false;
    }

    bool success = response->getBool("success");
    if (success) {
        session_token_ = response->getString("token"); // server šalje "token"
        current_urn_   = urn;
        authenticated_ = true;
        logger_->info("Authentication successful");
    } else {
        logger_->warning("Authentication failed");
    }

    return success;
}

void UserInterface::startInteractiveSession() {
    std::string input;
    std::cout << "\nTransport Protocol Client - Interactive Session\n";
    std::cout << "Type 'help' for available commands\n\n";

    while (true) {
        std::cout << "transport> ";
        std::getline(std::cin, input);

        if (input == "quit" || input == "exit") {
            break;
        } else if (input == "help") {
            showHelp();
        } else if (input.rfind("register_device", 0) == 0) {
            handleRegisterDevice(input);
        } else if (input.rfind("register", 0) == 0) {
            handleRegister(input);
        } else if (input.rfind("authenticate", 0) == 0) {
            handleAuthenticate(input);
        } else if (input == "listen") {
            handleListen(input);
        } else if (input.rfind("reserve", 0) == 0) {
            handleReserve(input);
        } else if (input.rfind("purchase", 0) == 0) {
            handlePurchase(input);
        } else if (input.rfind("create_group", 0) == 0) {
            handleCreateGroup(input);
        } else if (input.rfind("add_member", 0) == 0) {
            handleAddMember(input);
        } else if (input.rfind("rm_member", 0) == 0) {
            handleRemoveMember(input);
        } else if (input.empty()) {
            continue;
        } else {
            std::cout << "Unknown command: " << input << ". Type 'help' for available commands.\n";
        }
    }

    std::cout << "Goodbye!\n";
}

void UserInterface::setLogLevel(Logger::LogLevel level) {
    if (logger_) {
        logger_->setLogLevel(level);
    }
}

void UserInterface::showHelp() {
    std::cout << "\nAvailable commands:\n";
    std::cout << "  register <URN>                        - Register user with 13-digit URN\n";
    std::cout << "  authenticate <URN>                    - Authenticate with URN\n";
    std::cout << "  register_device <URI> <vehicle>       - Register a vehicle device (bus/tram/trolleybus)\n";
    std::cout << "  reserve <vehicle> <URI>               - Reserve seat by URI (bus/tram/trolleybus)\n";
    std::cout << "  purchase <type> <vehicle> <URI> [n]   - Purchase ticket(s) by URI\n";
    std::cout << "    - type: individual/family/business/tourist\n";
    std::cout << "    - vehicle: bus/tram/trolleybus\n";
    std::cout << "    - URI: either 'uri=bus125' or just 'bus125'\n";
    std::cout << "    - n: number of seats (default 1)\n";
    std::cout << "  listen                                - Listen for async multicast updates\n";
    std::cout << "  create_group <name> <leader_urn>      - Create user group\n";
    std::cout << "  add_member <name> <member_urn>        - Add member to group\n";
    std::cout << "  rm_member <name> <member_urn>         - Remove member from group (leader only)\n";
    std::cout << "  help                                  - Show this help\n";
    std::cout << "  quit                                  - Exit the client\n\n";
}

void UserInterface::handleRegister(const std::string& input) {
    auto parts = splitString(input, ' ');
    if (parts.size() != 2) {
        std::cout << "Usage: register <URN>\n";
        return;
    }

    std::string urn = parts[1];
    if (urn.length() != 13 || urn.find_first_not_of("0123456789") != std::string::npos) {
        std::cout << "Error: URN must be exactly 13 digits\n";
        return;
    }

    auto message = MessageFactory::createRegisterUser(urn);
    if (socket_ && socket_->sendMessage(*message)) {
        auto response = socket_->receiveMessage();
        if (response && response->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "User registered successfully!\n";
        } else {
            std::cout << "Registration failed: "
                      << (response ? response->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send registration request\n";
    }
}

void UserInterface::handleRegisterDevice(const std::string& input) {
    if (!authenticated_) {
        std::cout << "Please authenticate first\n";
        return;
    }
    auto parts = splitString(input, ' ');
    if (parts.size() != 3) {
        std::cout << "Usage: register_device <URI> <vehicle>\n";
        return;
    }
    VehicleType vehicle_type;
    if (parts[2] == "bus") vehicle_type = VehicleType::BUS;
    else if (parts[2] == "tram") vehicle_type = VehicleType::TRAM;
    else if (parts[2] == "trolleybus") vehicle_type = VehicleType::TROLLEYBUS;
    else {
        std::cout << "Invalid vehicle type. Use: bus, tram, or trolleybus\n";
        return;
    }
    auto message = MessageFactory::createRegisterDevice(parts[1], vehicle_type);
    if (socket_ && socket_->sendMessage(*message)) {
        auto response = socket_->receiveMessage();
        if (response && response->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "Device registered successfully!\n";
        } else {
            std::cout << "Device registration failed: "
                      << (response ? response->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send device registration request\n";
    }
}

void UserInterface::handleAuthenticate(const std::string& input) {
    auto parts = splitString(input, ' ');
    if (parts.size() != 2) {
        std::cout << "Usage: authenticate <URN>\n";
        return;
    }

    std::string urn = parts[1];
    if (authenticate(urn)) {
        std::cout << "Authentication successful!\n";
    } else {
        std::cout << "Authentication failed!\n";
    }
}

static std::string normalizeUriArg(const std::string& s) {
    if (s.rfind("uri=", 0) == 0) return s.substr(4);
    return s;
}

void UserInterface::handleReserve(const std::string& input) {
    if (!authenticated_) {
        std::cout << "Please authenticate first\n";
        return;
    }

    auto parts = splitString(input, ' ');
    if (parts.size() != 3) {
        std::cout << "Usage: reserve <vehicle> <URI>\n";
        return;
    }

    VehicleType vehicle_type;
    if (parts[1] == "bus") vehicle_type = VehicleType::BUS;
    else if (parts[1] == "tram") vehicle_type = VehicleType::TRAM;
    else if (parts[1] == "trolleybus") vehicle_type = VehicleType::TROLLEYBUS;
    else {
        std::cout << "Invalid vehicle type. Use: bus, tram, or trolleybus\n";
        return;
    }

    const std::string uri = normalizeUriArg(parts[2]);

    
    auto message = MessageFactory::createReserveSeat(vehicle_type, "" /*unused*/);
    message->addString("uri", uri);
    if (!current_urn_.empty()) message->addString("urn", current_urn_);
    message->calculateChecksum();

    if (socket_ && socket_->sendMessage(*message)) {
        auto response = socket_->receiveMessage();
        if (response && response->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "Seat reserved successfully!\n";
        } else {
            std::cout << "Reservation failed: "
                      << (response ? response->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send reservation request\n";
    }
}

void UserInterface::handlePurchase(const std::string& input) {
    if (!authenticated_) {
        std::cout << "Please authenticate first\n";
        return;
    }

    auto parts = splitString(input, ' ');
    if (parts.size() < 4) {
        std::cout << "Usage: purchase <type> <vehicle> <URI> [passengers]\n";
        return;
    }

    TicketType ticket_type;
    if (parts[1] == "individual") ticket_type = TicketType::INDIVIDUAL;
    else if (parts[1] == "family") ticket_type = TicketType::GROUP_FAMILY;
    else if (parts[1] == "business") ticket_type = TicketType::GROUP_BUSINESS;
    else if (parts[1] == "tourist") ticket_type = TicketType::GROUP_TOURIST;
    else {
        std::cout << "Invalid ticket type. Use: individual, family, business, or tourist\n";
        return;
    }

    VehicleType vehicle_type;
    if (parts[2] == "bus") vehicle_type = VehicleType::BUS;
    else if (parts[2] == "tram") vehicle_type = VehicleType::TRAM;
    else if (parts[2] == "trolleybus") vehicle_type = VehicleType::TROLLEYBUS;
    else {
        std::cout << "Invalid vehicle type. Use: bus, tram, or trolleybus\n";
        return;
    }

    const std::string uri = normalizeUriArg(parts[3]);

    int passengers = 1;
    if (parts.size() > 4) {
        try {
            passengers = std::stoi(parts[4]);
        } catch (...) {
            std::cout << "Invalid passengers number\n";
            return;
        }
        if (passengers < 1) passengers = 1;
    }

  
    auto message = MessageFactory::createPurchaseTicket(ticket_type, vehicle_type, "" /*unused*/, passengers);
    message->addString("uri", uri);
    if (!session_token_.empty()) message->addString("session_id", session_token_);
    message->calculateChecksum();

    if (socket_ && socket_->sendMessage(*message)) {
        auto response = socket_->receiveMessage();
        if (response && response->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "Ticket purchased successfully!\n";
        } else {
            std::cout << "Purchase failed: "
                      << (response ? response->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send purchase request\n";
    }
}

void UserInterface::handleCreateGroup(const std::string& input) {
    if (!authenticated_) {
        std::cout << "Please authenticate first\n";
        return;
    }

    auto parts = splitString(input, ' ');
    if (parts.size() != 3) {
        std::cout << "Usage: create_group <name> <leader_urn>\n";
        return;
    }

    auto message = MessageFactory::createGroupCreate(parts[1], parts[2]);
    if (socket_ && socket_->sendMessage(*message)) {
        auto response = socket_->receiveMessage();
        if (response && response->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "Group created successfully!\n";
        } else {
            std::cout << "Group creation failed: "
                      << (response ? response->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send create_group request\n";
    }
}

void UserInterface::handleAddMember(const std::string& input) {
    if (!authenticated_) {
        std::cout << "Please authenticate first\n";
        return;
    }
    auto parts = splitString(input, ' ');
    if (parts.size() != 3) {
        std::cout << "Usage: add_member <group_name> <member_urn>\n";
        return;
    }
    const std::string& group = parts[1];
    const std::string& urn   = parts[2];

    Message m(MessageType::ADD_MEMBER_TO_GROUP);
    m.addString("session_id", session_token_);
    m.addString("group_name", group);
    m.addString("urn", urn);
    m.calculateChecksum();

    if (socket_ && socket_->sendMessage(m)) {
        auto resp = socket_->receiveMessage();
        if (resp && resp->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "User added to group\n";
        } else {
            std::cout << "Failed to add member: "
                      << (resp ? resp->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send add_member request\n";
    }
}

void UserInterface::handleRemoveMember(const std::string& input) {
    if (!authenticated_) {
        std::cout << "Please authenticate first\n";
        return;
    }
    auto parts = splitString(input, ' ');
    if (parts.size() != 3) {
        std::cout << "Usage: rm_member <group_name> <member_urn>\n";
        return;
    }

    std::cout << "(Note: only group leader can successfully remove members)\n";

    const std::string& group = parts[1];
    const std::string& urn   = parts[2];

    Message m(MessageType::DELETE_GROUP_MEMBER);
    m.addString("session_id", session_token_);
    m.addString("group_name", group);
    m.addString("urn", urn);
    m.calculateChecksum();

    if (socket_ && socket_->sendMessage(m)) {
        auto resp = socket_->receiveMessage();
        if (resp && resp->getType() == MessageType::RESPONSE_SUCCESS) {
            std::cout << "User removed from group\n";
        } else {
            std::cout << "Failed to remove member: "
                      << (resp ? resp->getString("error") : "No response") << "\n";
        }
    } else {
        std::cout << "Failed to send rm_member request\n";
    }
}

void UserInterface::handleListen(const std::string& /*input*/) {
    std::cout << "Listening for async updates... (Ctrl+C to stop)\n";
    while (true) {
        auto msg = socket_->receiveMessage();
        if (!msg) {
            std::cout << "Listener stopped or connection closed\n";
            break;
        }
        if (msg->getType() == MessageType::MULTICAST_UPDATE) {
            std::cout << "[Update] " << msg->getString("update_type") << "\n";
        } else {
            std::cout << "[Async] type=" << static_cast<int>(msg->getType()) << "\n";
        }
    }
}

std::vector<std::string> UserInterface::splitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);

    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

// ---------------------- UDP multicast

std::optional<std::pair<std::string,int>> UserInterface::discoverServer(int timeout_ms) {
    using boost::asio::ip::udp;

    try {
        boost::asio::io_context io;

        udp::socket sock(io);
        sock.open(udp::v4());

        sock.set_option(boost::asio::ip::multicast::enable_loopback(true));
        sock.bind(udp::endpoint(udp::v4(), 0));

        udp::endpoint mcast_ep(boost::asio::ip::make_address(MCAST_ADDR), MCAST_PORT);

        static const char kDiscover[] = "DISCOVER";
        sock.send_to(boost::asio::buffer(kDiscover, sizeof(kDiscover)-1), mcast_ep);

        std::string answer;
        udp::endpoint from;

        boost::asio::steady_timer timer(io);
        bool got_reply = false;

        std::array<char, 256> buf{};

        sock.async_receive_from(
            boost::asio::buffer(buf), from,
            [&](const boost::system::error_code& ec, std::size_t n) {
                if (!ec && n > 0) {
                    answer.assign(buf.data(), buf.data() + n);
                    got_reply = true;
                    timer.cancel();
                }
            }
        );

        timer.expires_after(std::chrono::milliseconds(timeout_ms));
        timer.async_wait([&](const boost::system::error_code&){ sock.cancel(); });

        io.run();

        if (!got_reply) {
            return std::nullopt;
        }

        //  "ANNOUNCE central <port>"
        int port = 0;
        {
            std::istringstream iss(answer);
            std::string a, who;
            iss >> a >> who >> port; // ANNOUNCE central 12345
        }
        if (port <= 0 || port > 65535) {
            logger_->warning("Malformed ANNOUNCE: '" + answer + "'");
            return std::nullopt;
        }
        return std::make_pair(from.address().to_string(), port);
    } catch (const std::exception& e) {
        if (logger_) logger_->warning(std::string("discoverServer failed: ") + e.what());
        return std::nullopt;
    }
}

} // namespace transport

