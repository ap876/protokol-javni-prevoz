#include "server/CentralServer.h"
#include "common/Logger.h"
#include "common/Message.h"  // MessageType / MessageFactory

#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <atomic>
#include <map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>

namespace transport {

namespace {

// Default multicast
static constexpr const char*    DEFAULT_MCAST_ADDR = "239.192.0.1";
static constexpr unsigned short DEFAULT_MCAST_PORT = 30001;

// Thread-safe lokalno vrijeme → std::tm
inline std::tm localtime_safe(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// --- Helpers za log ---

inline const char* messageTypeToString(MessageType t) {
    switch (t) {
        case MessageType::CONNECT_REQUEST:      return "CONNECT_REQUEST";
        case MessageType::CONNECT_RESPONSE:     return "CONNECT_RESPONSE";
        case MessageType::AUTH_REQUEST:         return "AUTH_REQUEST";
        case MessageType::AUTH_RESPONSE:        return "AUTH_RESPONSE";
        case MessageType::REGISTER_USER:        return "REGISTER_USER";
        case MessageType::REGISTER_DEVICE:      return "REGISTER_DEVICE";
        case MessageType::RESERVE_SEAT:         return "RESERVE_SEAT";
        case MessageType::PURCHASE_TICKET:      return "PURCHASE_TICKET";
        case MessageType::CREATE_GROUP:         return "CREATE_GROUP";
        case MessageType::DELETE_USER:          return "DELETE_USER";
        case MessageType::DELETE_GROUP_MEMBER:  return "DELETE_GROUP_MEMBER";
        case MessageType::UPDATE_PRICE_LIST:    return "UPDATE_PRICE_LIST (deprecated)";
        case MessageType::GET_VEHICLE_STATUS:   return "GET_VEHICLE_STATUS";
        case MessageType::MULTICAST_UPDATE:     return "MULTICAST_UPDATE";
        case MessageType::RESPONSE_SUCCESS:     return "RESPONSE_SUCCESS";
        case MessageType::RESPONSE_ERROR:       return "RESPONSE_ERROR";
        case MessageType::HEARTBEAT:            return "HEARTBEAT";
        case MessageType::DISCONNECT:           return "DISCONNECT";
        case MessageType::UPDATE_PRICE:         return "UPDATE_PRICE";
        case MessageType::UPDATE_VEHICLE:       return "UPDATE_VEHICLE";
        case MessageType::UPDATE_CAPACITY:      return "UPDATE_CAPACITY";
        case MessageType::ADD_MEMBER_TO_GROUP:  return "ADD_MEMBER_TO_GROUP";
        default:                                return "<unknown>";
    }
}

inline const char* vehicleTypeToString(VehicleType v) {
    switch (v) {
        case VehicleType::BUS:        return "BUS";
        case VehicleType::TRAM:       return "TRAM";
        case VehicleType::TROLLEYBUS: return "TROLLEYBUS";
        default:                      return "<unknown>";
    }
}

inline const char* ticketTypeToString(TicketType t) {
    switch (t) {
        case TicketType::INDIVIDUAL:     return "INDIVIDUAL";
        case TicketType::GROUP_FAMILY:   return "GROUP_FAMILY";
        case TicketType::GROUP_BUSINESS: return "GROUP_BUSINESS";
        case TicketType::GROUP_TOURIST:  return "GROUP_TOURIST";
        default:                         return "<unknown>";
    }
}

} // namespace

// =========================
// CentralServer
// =========================

CentralServer::CentralServer()
    : ServerBase("CentralServer") {
    // default config (po potrebi prepisuje loadConfiguration)
    config_.max_connections          = 1000;
    config_.heartbeat_interval       = 30;
    config_.session_timeout          = 3600;
    config_.data_collection_interval = 60;
    config_.enable_multicast         = false; 
    config_.multicast_address        = DEFAULT_MCAST_ADDR;
    config_.multicast_port           = DEFAULT_MCAST_PORT;
}

CentralServer::~CentralServer() {
    stop();
}

bool CentralServer::start(int port, const std::string& config_file) {
    port_ = port;
    if (!config_file.empty()) {
        loadConfiguration(config_file);
    }
    if (!initializeDatabase()) {
        logError("Failed to initialize database");
        return false;
    }

    // TLS server
    tls_server_ = std::make_unique<TLSServer>();
    tls_server_->setConnectionCallback([this](std::unique_ptr<TLSSocket> client) {
        std::thread([this, c = std::move(client)]() mutable {
            handleClientMessage(std::move(c), nullptr);
        }).detach();
    });
    if (!tls_server_->start(port, cert_file_, key_file_)) {
        logError("Failed to start TLS server on port " + std::to_string(port));
        return false;
    }

    if (config_.enable_multicast) {
        if (!setupMulticast()) {
            logWarning("Multicast discovery not started; continuing without it");
        }
    }

    running_    = true;
    start_time_ = std::chrono::system_clock::now();
    startBackgroundTasks();
    logInfo("Central Server started on port " + std::to_string(port));
    return true;
}

void CentralServer::stop() {
    if (!running_) return;
    running_ = false;

    stopBackgroundTasks();
    cleanupMulticast();

    if (tls_server_) {
        tls_server_->stop();
    }
    logInfo("Central Server stopped");
}

bool CentralServer::loadConfiguration(const std::string& config_file) {
    logInfo("Loading configuration from: " + config_file);
    return true;
}

bool CentralServer::initializeDatabase() {
    auto& pool = DatabasePool::getInstance();
    return pool.initialize(db_path_.empty() ? "central_server.db" : db_path_, 5);
}

void CentralServer::startBackgroundTasks() {
    background_running_     = true;
    data_collection_thread_ = std::make_unique<std::thread>(&CentralServer::dataCollectionLoop, this);
    heartbeat_thread_       = std::make_unique<std::thread>(&CentralServer::heartbeatLoop, this);
    cleanup_thread_         = std::make_unique<std::thread>(&CentralServer::sessionCleanupLoop, this);
}

void CentralServer::stopBackgroundTasks() {
    background_running_ = false;
    if (data_collection_thread_ && data_collection_thread_->joinable()) data_collection_thread_->join();
    if (heartbeat_thread_       && heartbeat_thread_->joinable())       heartbeat_thread_->join();
    if (cleanup_thread_         && cleanup_thread_->joinable())         cleanup_thread_->join();
}

void CentralServer::handleClientMessage(std::unique_ptr<TLSSocket> client, std::unique_ptr<Message> /* message */) {
    if (!client) return;

    total_connections_++;
    active_connections_++;

    logInfo("New client connected from " + client->getPeerAddress() + ":" + std::to_string(client->getPeerPort()));

    while (running_ && client) {
        auto received_message = client->receiveMessage();
        if (!received_message) break;
        logDebug(std::string("Incoming message type: ") +
                 messageTypeToString(received_message->getType()));
        processMessage(std::move(received_message), client);
    }

    active_connections_--;
    {
        std::lock_guard<std::mutex> lock(broadcast_mutex_);
        auto it = std::remove(subscribers_.begin(), subscribers_.end(), client.get());
        subscribers_.erase(it, subscribers_.end());
    }
    logInfo("Client disconnected");
}

void CentralServer::processMessage(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    if (!message || !client) return;

    const auto mt = message->getType();
    logInfo(std::string("Process: ") + messageTypeToString(mt));

    switch (mt) {
        case MessageType::CONNECT_REQUEST:     handleConnectRequest(std::move(message), client); break;
        case MessageType::AUTH_REQUEST:        handleAuthRequest(std::move(message), client); break;
        case MessageType::REGISTER_USER:       handleUserRegistration(std::move(message), client); break;
        case MessageType::REGISTER_DEVICE:     handleDeviceRegistration(std::move(message), client); break;
        case MessageType::RESERVE_SEAT:        handleSeatReservation(std::move(message), client); break;
        case MessageType::PURCHASE_TICKET:     handleTicketPurchase(std::move(message), client); break;
        case MessageType::CREATE_GROUP:        handleGroupCreation(std::move(message), client); break;
        case MessageType::ADD_MEMBER_TO_GROUP: handleAddMemberToGroup(std::move(message), client); break;
        case MessageType::DELETE_GROUP_MEMBER: handleRemoveMemberFromGroup(std::move(message), client); break;
        case MessageType::DELETE_USER:         handleUserDeletion(std::move(message), client); break;

        // Administrativne poruke
        case MessageType::UPDATE_PRICE:        handleUpdatePrice(std::move(message), client); break;
        case MessageType::UPDATE_VEHICLE:      handleUpdateVehicle(std::move(message), client); break;
        case MessageType::UPDATE_CAPACITY:     handleUpdateCapacity(std::move(message), client); break;

        default:
            logWarning("Unknown/unsupported message type");
            sendErrorResponse(client, "Unknown message type");
            break;
    }
}

void CentralServer::handleConnectRequest(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    std::string client_id = message->getString("client_id");
    logInfo("CONNECT_REQUEST from client_id=" + (client_id.empty() ? "<unknown>" : client_id));
    auto response = MessageFactory::createConnectResponse(true, "Connection established");
    sendResponse(client, std::move(response));
}

void CentralServer::handleAuthRequest(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    const std::string urn = message->getString("urn"); // bez PIN-a
    logInfo("AUTH_REQUEST urn=" + (urn.empty() ? "<missing>" : urn));

    auto db = DatabasePool::getInstance().getConnection();
    bool authenticated = false;

    if (!urn.empty()) {
        authenticated = static_cast<bool>(db->getUser(urn)); // postoji user?
    }

    std::string session_id = authenticated ? generateSessionId() : "";
    auto response = MessageFactory::createAuthResponse(authenticated, session_id);

    if (authenticated) {
        ClientSession s;
        s.session_id    = session_id;
        s.user_urn      = urn;
        s.authenticated = true;
        s.last_activity = std::chrono::system_clock::now();
        s.socket        = nullptr;

        {
            std::lock_guard<std::mutex> lk(sessions_mutex_);
            client_sessions_[session_id] = std::move(s);
        }
    }

    sendResponse(client, std::move(response));

    if (authenticated) {
        logInfo("User authenticated: " + urn + " (session_id=" + session_id + ")");
        std::lock_guard<std::mutex> lock(broadcast_mutex_);
        subscribers_.push_back(client.get());
    } else {
        logWarning("Authentication failed for URN: " + urn);
    }

    DatabasePool::getInstance().returnConnection(db);
}

void CentralServer::handleUserRegistration(std::unique_ptr<Message> message,
                                           std::unique_ptr<TLSSocket>& client) {
    std::string urn = message->getString("urn");
    logInfo("REGISTER_USER urn=" + (urn.empty() ? "<missing>" : urn));

    if (!validateURN(urn)) {
        logWarning("REGISTER_USER invalid URN: " + urn);
        sendErrorResponse(client, "Invalid URN format", 400);
        return;
    }

    auto db = DatabasePool::getInstance().getConnection();

    if (auto exists = db->getUser(urn)) {
        DatabasePool::getInstance().returnConnection(db);
        logInfo("REGISTER_USER already exists: " + urn);
        sendErrorResponse(client, "User already registered", 409);
        return;
    }

    User user;
    user.urn               = urn;
    user.name              = message->hasKey("name") ? message->getString("name") : ("User_" + urn);
    user.age               = message->hasKey("age")  ? message->getInt("age")     : 25;
    user.registration_date = getCurrentTimestamp();
    user.active            = true;
    user.pin_hash          = message->hasKey("pin_hash") ? message->getString("pin_hash") : "default_hash";

    bool ok    = db->registerUser(user);
    auto dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (ok) {
        logInfo("User registered: " + urn);
        sendSuccessResponse(client, "User registered successfully");
    } else {
        logError("Failed to register user: " + urn + (dbErr.empty()? "" : (" | " + dbErr)));
        sendErrorResponse(client, dbErr.empty() ? "Failed to register user" : dbErr, 500);
    }
}

void CentralServer::handleDeviceRegistration(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    std::string uri  = message->getString("uri");
    int vtype        = message->getInt("vehicle_type");
    logInfo("REGISTER_DEVICE uri=" + (uri.empty()?"<missing>":uri) +
            ", vehicle_type=" + std::to_string(vtype));

    if (!validateURI(uri)) {
        logWarning("REGISTER_DEVICE invalid URI: " + uri);
        sendErrorResponse(client, "Invalid URI format", 400);
        return;
    }

    Vehicle vehicle;
    vehicle.uri             = uri;
    vehicle.type            = static_cast<VehicleType>(vtype);
    vehicle.capacity        = 50;
    vehicle.available_seats = 50;
    vehicle.route           = "Route_" + uri; // server generiše rutu po URI
    vehicle.active          = true;
    vehicle.last_update     = getCurrentTimestamp();

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->registerVehicle(vehicle);
    const std::string dbErr = db->getLastError();
    const int dbCode        = db->getLastErrorCode();
    DatabasePool::getInstance().returnConnection(db);

    if (ok) {
        logInfo("Device registered: " + uri + " (route=" + vehicle.route + ")");
        sendSuccessResponse(client, "Device registered successfully");
    } else {
        if (dbCode == SQLITE_CONSTRAINT || dbErr.find("exists") != std::string::npos) {
            logInfo("Device already exists (constraint): " + uri);
            sendErrorResponse(client, "Device already exists", 409);
        } else {
            logError("Failed to register device: " + uri + (dbErr.empty() ? "" : (" | " + dbErr)));
            sendErrorResponse(client, dbErr.empty() ? "Failed to register device" : dbErr, 500);
        }
    }
}

void CentralServer::handleSeatReservation(std::unique_ptr<Message> message,
                                          std::unique_ptr<TLSSocket>& client) {
    VehicleType vehicle_type = static_cast<VehicleType>(message->getInt("vehicle_type"));
    std::string route = message->hasKey("route") ? message->getString("route") : "";
    std::string uri   = message->hasKey("uri")   ? message->getString("uri")   : "";
    std::string urn   = message->hasKey("urn")   ? message->getString("urn")   : "";

    logInfo("RESERVE_SEAT req: urn=" + (urn.empty()?"<missing>":urn) +
            ", vt=" + std::string(vehicleTypeToString(vehicle_type)) +
            ", route=" + (route.empty()? "<none>" : route) +
            ", uri=" + (uri.empty()? "<none>" : uri));

    if (urn.empty()) {
        logWarning("RESERVE_SEAT rejected: missing URN");
        sendErrorResponse(client, "Missing user URN");
        return;
    }

    auto db = DatabasePool::getInstance().getConnection();
    std::unique_ptr<Vehicle> vehicle;

    if (!uri.empty()) {
        if (auto v = db->getVehicle(uri)) {
            vehicle      = std::move(v);
            vehicle_type = vehicle->type;
            route        = vehicle->route;
        }
    }

    if (!vehicle && !route.empty()) {
        vehicle = db->getVehicleByRouteAndType(route, vehicle_type);
        if (!vehicle) {
            for (VehicleType t : {VehicleType::BUS, VehicleType::TRAM, VehicleType::TROLLEYBUS}) {
                if (t == vehicle_type) continue;
                if (auto v2 = db->getVehicleByRouteAndType(route, t)) {
                    vehicle      = std::move(v2);
                    vehicle_type = t;
                    break;
                }
            }
        }
    }

    if (!vehicle) {
        DatabasePool::getInstance().returnConnection(db);
        logWarning("RESERVE_SEAT failed: vehicle/route not found (route=" +
                   (route.empty()?"<none>":route) + ", uri=" + (uri.empty()?"<none>":uri) + ")");
        sendErrorResponse(client, "Vehicle/route not found");
        return;
    }
    if (route.empty()) route = vehicle->route;

    if (vehicle->available_seats <= 0) {
        DatabasePool::getInstance().returnConnection(db);
        logInfo("RESERVE_SEAT rejected: no seats (uri=" + vehicle->uri + ", route=" + route + ")");
        sendErrorResponse(client, "No available seats for this route/vehicle");
        return;
    }

    const int new_available = vehicle->available_seats - 1;
    if (!db->updateSeatAvailability(vehicle->uri, new_available)) {
        auto err = db->getLastError();
        DatabasePool::getInstance().returnConnection(db);
        logError("RESERVE_SEAT DB error(update seats): " + (err.empty()?"<unknown>":err));
        sendErrorResponse(client, "Failed to reserve seat");
        return;
    }
    DatabasePool::getInstance().returnConnection(db);

    logInfo("Seat reserved: urn=" + urn + ", uri=" + vehicle->uri + ", route=" + route +
            ", remaining=" + std::to_string(new_available));

    auto resp = MessageFactory::createSuccessResponse("Seat reserved successfully", {
        {"route", route},
        {"vehicle_uri", vehicle->uri},
        {"available_seats", std::to_string(new_available)}
    });
    sendResponse(client, std::move(resp));

    sendMulticastUpdate("seat_reserved", {
        {"route", route},
        {"vehicle_uri", vehicle->uri},
        {"available_seats", std::to_string(new_available)}
    });
}

void CentralServer::handleTicketPurchase(std::unique_ptr<Message> message,
                                         std::unique_ptr<TLSSocket>& client) {
    std::string urn;
    if (message->hasKey("session_id")) {
        const std::string sid = message->getString("session_id");
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = client_sessions_.find(sid);
        if (it == client_sessions_.end()) {
            logWarning("PURCHASE_TICKET rejected: invalid/expired session");
            sendErrorResponse(client, "Invalid or expired session", 401);
            return;
        }
        it->second.last_activity = std::chrono::system_clock::now();
        urn = it->second.user_urn;
    } else if (message->hasKey("urn")) {
        urn = message->getString("urn");
    }
    if (urn.empty()) {
        logWarning("PURCHASE_TICKET rejected: missing identity");
        sendErrorResponse(client, "Missing user identity (session_id or urn)", 400);
        return;
    }

    const TicketType ticket_type = static_cast<TicketType>(message->getInt("ticket_type"));
    VehicleType vehicle_type     = static_cast<VehicleType>(message->getInt("vehicle_type"));
    std::string route            = message->hasKey("route") ? message->getString("route") : "";
    const std::string uri        = message->hasKey("uri")   ? message->getString("uri")   : "";
    int passengers               = message->hasKey("passengers") ? message->getInt("passengers") : 1;
    if (passengers < 1) passengers = 1;

    logInfo(std::string("PURCHASE_TICKET req: urn=") + urn +
            ", tt=" + ticketTypeToString(ticket_type) +
            ", vt=" + vehicleTypeToString(vehicle_type) +
            ", route=" + (route.empty()?"<none>":route) +
            ", uri=" + (uri.empty()?"<none>":uri) +
            ", pax=" + std::to_string(passengers));

    auto db = DatabasePool::getInstance().getConnection();
    std::unique_ptr<Vehicle> vehicle;

    if (!uri.empty()) {
        auto v = db->getVehicle(uri);
        if (v) { vehicle = std::move(v); vehicle_type = vehicle->type; route = vehicle->route; }
    }
    if (!vehicle && !route.empty()) {
        vehicle = db->getVehicleByRouteAndType(route, vehicle_type);
    }

    if (!vehicle) {
        DatabasePool::getInstance().returnConnection(db);
        logWarning("PURCHASE_TICKET failed: vehicle/route not found (route=" +
                   (route.empty()?"<none>":route) + ", uri=" + (uri.empty()?"<none>":uri) + ")");
        sendErrorResponse(client, "Vehicle/route not found", 404);
        return;
    }
    if (route.empty()) route = vehicle->route;

    if (vehicle->available_seats < passengers) {
        DatabasePool::getInstance().returnConnection(db);
        logInfo("PURCHASE_TICKET rejected: not enough seats (uri=" + vehicle->uri +
                ", route=" + route + ", need=" + std::to_string(passengers) +
                ", have=" + std::to_string(vehicle->available_seats) + ")");
        sendErrorResponse(client, "Insufficient seats available", 409);
        return;
    }

    // Cijena (placeholder iz DB-a)
    const double price_each   = db->calculateTicketPrice(vehicle_type, ticket_type, 1, 1.0, 30.0);
    const double discount     = 0.0;
    const double total_amount = price_each * passengers;
    const std::string when_buy = getCurrentTimestamp();

    // Kreiraj karte
    std::vector<std::string> ticket_ids;
    ticket_ids.reserve(passengers);

    for (int i = 0; i < passengers; ++i) {
        Ticket t{};
        t.ticket_id     = generateTicketId();
        t.user_urn      = urn;
        t.type          = ticket_type;
        t.vehicle_type  = vehicle_type;
        t.route         = route;
        t.price         = price_each;
        t.discount      = discount;
        t.purchase_date = when_buy;
        t.seat_number   = std::to_string(vehicle->capacity - vehicle->available_seats + i + 1);
        t.used          = false;

        if (!db->createTicket(t)) {
            std::string err = db->getLastError();
            DatabasePool::getInstance().returnConnection(db);
            logError("PURCHASE_TICKET DB error(createTicket): " + (err.empty()?"<unknown>":err));
            sendErrorResponse(client, "Failed to create ticket record" + (err.empty() ? "" : (": " + err)), 500);
            return;
        }
        ticket_ids.push_back(t.ticket_id);
    }

    // Plaćanje – vežemo prvu kartu da FK nije prazan
    Payment p{};
    p.transaction_id = generateTransactionId();
    p.ticket_id      = ticket_ids.empty() ? "" : ticket_ids.front();
    p.amount         = total_amount;
    p.payment_method = "card";
    p.payment_date   = when_buy;
    p.successful     = true;

    if (!db->recordPayment(p)) {
        std::string err = db->getLastError();
        DatabasePool::getInstance().returnConnection(db);
        logError("PURCHASE_TICKET DB error(recordPayment): " + (err.empty()?"<unknown>":err));
        sendErrorResponse(client, "Failed to record payment" + (err.empty() ? "" : (": " + err)), 500);
        return;
    }

    // Update slobodnih mjesta
    const int new_available = vehicle->available_seats - passengers;
    if (!db->updateSeatAvailability(vehicle->uri, new_available)) {
        std::string err = db->getLastError();
        DatabasePool::getInstance().returnConnection(db);
        logError("PURCHASE_TICKET DB error(update seats): " + (err.empty()?"<unknown>":err));
        sendErrorResponse(client, "Failed to update seat availability" + (err.empty() ? "" : (": " + err)), 500);
        return;
    }

    DatabasePool::getInstance().returnConnection(db);

    logInfo("Ticket purchased: urn=" + urn + ", uri=" + vehicle->uri + ", route=" + route +
            ", pax=" + std::to_string(passengers) + ", total=" + std::to_string(total_amount) +
            ", remaining=" + std::to_string(new_available));

    auto resp = MessageFactory::createSuccessResponse("Ticket purchased successfully", {
        {"total_amount",     std::to_string(total_amount)},
        {"route",            route},
        {"vehicle_uri",      vehicle->uri},
        {"available_seats",  std::to_string(new_available)},
        {"passengers",       std::to_string(passengers)},
        {"user_urn",         urn}
    });
    sendResponse(client, std::move(resp));

    sendMulticastUpdate("ticket_purchased", {
        {"route",            route},
        {"vehicle_uri",      vehicle->uri},
        {"passengers",       std::to_string(passengers)},
        {"available_seats",  std::to_string(new_available)}
    });
}

void CentralServer::handleGroupCreation(std::unique_ptr<Message> message,
                                        std::unique_ptr<TLSSocket>& client) {
    const std::string group_name = message->getString("group_name");
    const std::string leader_urn = message->getString("leader_urn");

    logInfo("CREATE_GROUP name=" + (group_name.empty()?"<missing>":group_name) +
            ", leader=" + (leader_urn.empty()?"<missing>":leader_urn));

    if (group_name.empty() || leader_urn.empty()) {
        sendErrorResponse(client, "Missing group_name or leader_urn", 400);
        return;
    }

    Group g{};
    g.group_name    = group_name;
    g.leader_urn    = leader_urn;
    g.creation_date = getCurrentTimestamp();
    g.active        = true;

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->createGroup(g);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (ok) {
        logInfo("Group created: " + group_name + " (leader=" + leader_urn + ")");
        sendSuccessResponse(client, "Group created successfully");
    } else {
        logError("Failed to create group: " + group_name + (dbErr.empty()? "" : (" | " + dbErr)));
        sendErrorResponse(client, "Failed to create group", 500);
    }
}

void CentralServer::handleUserDeletion(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    // Možeš proširiti da stvarno briše; trenutno je stub u dnu (processUserDeletion)
    const std::string urn = message->hasKey("urn") ? message->getString("urn") : "";
    logInfo("DELETE_USER urn=" + (urn.empty()? "<missing>": urn));
    sendSuccessResponse(client, "User deletion request submitted");
}

// ======================= GROUP MEMBERSHIP =======================

void CentralServer::handleAddMemberToGroup(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    const std::string sid   = message->getString("session_id");
    const std::string urn   = message->getString("urn");
    const std::string group = message->getString("group_name");

    logInfo("ADD_MEMBER_TO_GROUP group=" + (group.empty()?"<missing>":group) +
            ", urn=" + (urn.empty()?"<missing>":urn) +
            ", session=" + (sid.empty()?"<missing>":sid));

    if (sid.empty() || group.empty() || urn.empty()) {
        sendErrorResponse(client, "Missing required fields (session_id, group_name, urn)", 400);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = client_sessions_.find(sid);
        if (it == client_sessions_.end()) {
            logWarning("ADD_MEMBER_TO_GROUP rejected: invalid/expired session");
            sendErrorResponse(client, "Invalid or expired session", 401);
            return;
        }
        it->second.last_activity = std::chrono::system_clock::now();
    }

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->addUserToGroup(urn, group);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (ok) {
        logInfo("Group member added: urn=" + urn + " -> " + group);
        sendSuccessResponse(client, "User added to group");
    } else {
        logError("Failed to add user to group: urn=" + urn + ", group=" + group +
                 (dbErr.empty()? "" : (" | " + dbErr)));
        sendErrorResponse(client, "Failed to add user to group", 500);
    }
}

bool CentralServer::requireGroupLeader(const std::string& session_id,
                                       const std::string& group_name,
                                       std::unique_ptr<TLSSocket>& client) {
    std::string caller_urn;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = client_sessions_.find(session_id);
        if (it == client_sessions_.end()) {
            logWarning("Group op rejected: invalid/expired session");
            sendErrorResponse(client, "Invalid or expired session", 401);
            return false;
        }
        caller_urn = it->second.user_urn;
        it->second.last_activity = std::chrono::system_clock::now();
    }

    auto db = DatabasePool::getInstance().getConnection();
    std::string leader = db->getGroupLeader(group_name);
    DatabasePool::getInstance().returnConnection(db);

    if (leader.empty()) {
        logWarning("Group op rejected: group not found or no leader set (" + group_name + ")");
        sendErrorResponse(client, "Group not found or no leader set", 404);
        return false;
    }
    if (leader != caller_urn) {
        logWarning("Group op rejected: not a leader (group=" + group_name + ", caller=" + caller_urn + ")");
        sendErrorResponse(client, "Admin (group leader) privileges required", 403);
        return false;
    }
    return true;
}

void CentralServer::handleRemoveMemberFromGroup(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client) {
    const std::string sid   = message->getString("session_id");
    const std::string urn   = message->getString("urn");
    const std::string group = message->getString("group_name");

    logInfo("DELETE_GROUP_MEMBER group=" + (group.empty()?"<missing>":group) +
            ", urn=" + (urn.empty()?"<missing>":urn) +
            ", session=" + (sid.empty()?"<missing>":sid));

    if (sid.empty() || group.empty() || urn.empty()) {
        sendErrorResponse(client, "Missing required fields (session_id, group_name, urn)", 400);
        return;
    }

    if (!requireGroupLeader(sid, group, client)) return;

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->removeUserFromGroup(urn, group);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (ok) {
        logInfo("Group member removed: urn=" + urn + " from " + group);
        sendSuccessResponse(client, "User removed from group");
    } else {
        logError("Failed to remove user from group: urn=" + urn + ", group=" + group +
                 (dbErr.empty()? "" : (" | " + dbErr)));
        sendErrorResponse(client, "Failed to remove user from group", 500);
    }
}

// ======================= ADMIN UPDATE HANDLERS =======================

void CentralServer::handleUpdatePrice(std::unique_ptr<Message> msg, std::unique_ptr<TLSSocket>& c) {
    if (!msg->hasKey("vehicle_type") || !msg->hasKey("ticket_type") || !msg->hasKey("price")) {
        logWarning("UPDATE_PRICE missing fields");
        return sendErrorResponse(c, "Missing vehicle_type/ticket_type/price", 400);
    }
    auto vt = static_cast<VehicleType>(msg->getInt("vehicle_type"));
    auto tt = static_cast<TicketType>(msg->getInt("ticket_type"));
    double price = 0.0;
    try { price = std::stod(msg->getString("price")); } catch (...) {
        logWarning("UPDATE_PRICE bad price format: " + msg->getString("price"));
        return sendErrorResponse(c, "Invalid price format", 400);
    }

    logInfo(std::string("UPDATE_PRICE vt=") + vehicleTypeToString(vt) +
            ", tt=" + ticketTypeToString(tt) + ", price=" + std::to_string(price));

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->updatePrice(vt, tt, price);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (!ok) {
        logError("UPDATE_PRICE failed: " + (dbErr.empty()? "<unknown>" : dbErr));
        return sendErrorResponse(c, dbErr.empty() ? "Failed to update price" : dbErr, 500);
    }

    sendSuccessResponse(c, "Price updated");
    sendMulticastUpdate("price_updated", {
        {"vehicle_type", std::to_string(static_cast<int>(vt))},
        {"ticket_type",  std::to_string(static_cast<int>(tt))},
        {"price",        msg->getString("price")}
    });
}

void CentralServer::handleUpdateVehicle(std::unique_ptr<Message> msg, std::unique_ptr<TLSSocket>& c) {
    if (!msg->hasKey("uri")) {
        logWarning("UPDATE_VEHICLE missing uri");
        return sendErrorResponse(c, "Missing uri", 400);
    }
    const std::string uri = msg->getString("uri");

    std::optional<bool> active;
    std::optional<std::string> route;
    std::optional<VehicleType> type;

    if (msg->hasKey("active")) active = (msg->getInt("active") != 0);
    if (msg->hasKey("route"))  route  = msg->getString("route");
    if (msg->hasKey("vehicle_type")) type = static_cast<VehicleType>(msg->getInt("vehicle_type"));

    std::ostringstream os;
    os << "UPDATE_VEHICLE uri=" << uri;
    if (active.has_value()) os << ", active=" << (*active ? "1" : "0");
    if (route.has_value())  os << ", route=" << *route;
    if (type.has_value())   os << ", type="  << vehicleTypeToString(*type);
    logInfo(os.str());

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->updateVehicle(uri, active, route, type);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (!ok) {
        logError("UPDATE_VEHICLE failed: " + (dbErr.empty()? "<unknown>" : dbErr));
        return sendErrorResponse(c, dbErr.empty() ? "Failed to update vehicle" : dbErr, 500);
    }

    sendSuccessResponse(c, "Vehicle updated");
    sendMulticastUpdate("vehicle_updated", {{"uri", uri}});
}

void CentralServer::handleUpdateCapacity(std::unique_ptr<Message> msg, std::unique_ptr<TLSSocket>& c) {
    if (!msg->hasKey("uri") || !msg->hasKey("capacity")) {
        logWarning("UPDATE_CAPACITY missing uri/capacity");
        return sendErrorResponse(c, "Missing uri/capacity", 400);
    }
    const std::string uri = msg->getString("uri");
    const int capacity    = msg->getInt("capacity");
    const int available   = msg->hasKey("available_seats") ? msg->getInt("available_seats") : capacity;

    logInfo("UPDATE_CAPACITY uri=" + uri +
            ", capacity=" + std::to_string(capacity) +
            ", available=" + std::to_string(available));

    auto db = DatabasePool::getInstance().getConnection();
    bool ok = db->updateVehicleCapacity(uri, capacity, available);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (!ok) {
        logError("UPDATE_CAPACITY failed: " + (dbErr.empty()? "<unknown>" : dbErr));
        return sendErrorResponse(c, dbErr.empty() ? "Failed to update capacity" : dbErr, 500);
    }

    sendSuccessResponse(c, "Capacity updated");
    sendMulticastUpdate("capacity_updated", {
        {"uri", uri},
        {"capacity", std::to_string(capacity)},
        {"available_seats", std::to_string(available)}
    });
}

// ======================= BACKGROUND / UTILS =======================

void CentralServer::dataCollectionLoop() {
    while (background_running_) {
        logDebug("Background: collectVehicleData()");
        collectVehicleData();
        std::this_thread::sleep_for(std::chrono::seconds(config_.data_collection_interval));
    }
}

void CentralServer::heartbeatLoop() {
    while (background_running_) {
        logDebug("Background: heartbeat()");
        std::this_thread::sleep_for(std::chrono::seconds(config_.heartbeat_interval));
    }
}

void CentralServer::sessionCleanupLoop() {
    while (background_running_) {
        logDebug("Background: sessionCleanup()");
        cleanupExpiredSessions();
        std::this_thread::sleep_for(std::chrono::seconds(300));
    }
}

void CentralServer::collectVehicleData() {

}

void CentralServer::cleanupExpiredSessions() {
    const auto now = std::chrono::system_clock::now();
    const auto ttl = std::chrono::seconds(config_.session_timeout);
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    for (auto it = client_sessions_.begin(); it != client_sessions_.end(); ) {
        if (!it->second.authenticated || (now - it->second.last_activity) > ttl) it = client_sessions_.erase(it);
        else ++it;
    }
}

std::string CentralServer::generateSessionId() {
    static std::atomic<int> counter{0};
    return "session_" + std::to_string(++counter);
}

std::string CentralServer::generateTicketId() {
    static std::atomic<int> counter{0};
    return std::string("TKT_") + std::to_string(++counter) + "_" + std::to_string(std::time(nullptr));
}

std::string CentralServer::generateTransactionId() {
    static std::atomic<int> counter{0};
    return std::string("TX_") + std::to_string(++counter) + "_" + std::to_string(std::time(nullptr));
}

std::string CentralServer::getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm = localtime_safe(now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

bool CentralServer::validateURN(const std::string& urn) {
    return urn.length() == 13 && urn.find_first_not_of("0123456789") == std::string::npos;
}

bool CentralServer::validateURI(const std::string& uri) {
    return !uri.empty() && uri.length() <= 32;
}

void CentralServer::setCertificatePath(const std::string& cert_path, const std::string& key_path) {
    cert_file_ = cert_path;
    key_file_  = key_path;
}

// --- Multicast: loguj sve što šalješ da bi se vidjelo u terminalu ---

void CentralServer::sendMulticastUpdate(const std::string& update_type,
                                        const std::map<std::string, std::string>& data) {
    // LOGUJ ŠTA ŠALJEŠ
    std::ostringstream os;
    os << "Broadcast: " << update_type << " {";
    bool first = true;
    for (auto& kv : data) {
        if (!first) os << ", ";
        os << kv.first << "=" << kv.second;
        first = false;
    }
    os << "}";
    logInfo(os.str());

    auto message = MessageFactory::createMulticastUpdate(update_type, data);
    std::lock_guard<std::mutex> lock(broadcast_mutex_);

    // pokušaj slanja svim subscriberima; ukloni nevažeće
    auto end_it = std::remove_if(subscribers_.begin(), subscribers_.end(),
        [&](TLSSocket* sub){
            if (!sub) return true;
            bool ok = false;
            try {
                ok = sub->sendMessage(*message);
            } catch (...) {
                ok = false;
            }
            if (!ok) logWarning("Broadcast drop: one subscriber not reachable");
            return !ok;
        });
    subscribers_.erase(end_it, subscribers_.end());
}

// ======================= UDP Multicast (Boost.Asio) =======================

bool CentralServer::setupMulticast() {
    using boost::asio::ip::udp;

    try {
        mcast_io_   = std::make_unique<boost::asio::io_context>();
        mcast_sock_ = std::make_unique<udp::socket>(*mcast_io_);

        mcast_listen_ep_ = udp::endpoint(udp::v4(), static_cast<unsigned short>(config_.multicast_port));
        mcast_sock_->open(udp::v4());
        mcast_sock_->set_option(boost::asio::socket_base::reuse_address(true));
        mcast_sock_->bind(mcast_listen_ep_);

        auto group_addr = boost::asio::ip::make_address(config_.multicast_address);
        mcast_sock_->set_option(boost::asio::ip::multicast::join_group(group_addr));
        mcast_sock_->set_option(boost::asio::ip::multicast::enable_loopback(true));

        mcast_group_ = udp::endpoint(group_addr, static_cast<unsigned short>(config_.multicast_port));

        startMulticastReceive_();

        mcast_thread_ = std::make_unique<std::thread>([io = mcast_io_.get()](){
            try { io->run(); } catch (const std::exception&) {}
        });

        logInfo("Multicast discovery started on " + config_.multicast_address + ":" + std::to_string(config_.multicast_port));
        return true;
    } catch (const std::exception& e) {
        logWarning(std::string("setupMulticast failed: ") + e.what());
        mcast_sock_.reset(); mcast_io_.reset(); mcast_thread_.reset();
        return false;
    }
}

void CentralServer::cleanupMulticast() {
    try {
        if (mcast_sock_) {
            boost::system::error_code ec;
            try {
                auto group_addr = boost::asio::ip::make_address(config_.multicast_address);
                mcast_sock_->set_option(boost::asio::ip::multicast::leave_group(group_addr), ec);
            } catch (...) {}
            mcast_sock_->close(ec);
        }
        if (mcast_io_) mcast_io_->stop();
        if (mcast_thread_ && mcast_thread_->joinable()) mcast_thread_->join();
    } catch (...) {}
    mcast_sock_.reset(); mcast_io_.reset(); mcast_thread_.reset();
}

void CentralServer::startMulticastReceive_() {
    using boost::asio::ip::udp;
    if (!mcast_sock_) return;

    auto sender = std::make_shared<udp::endpoint>();
    mcast_sock_->async_receive_from(
        boost::asio::buffer(mcast_rx_buf_), *sender,
        [this, sender](const boost::system::error_code& ec, std::size_t n) {
            if (!mcast_sock_) return;
            if (!ec && n > 0) {
                std::string msg(mcast_rx_buf_.data(), mcast_rx_buf_.data() + n);

                while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' '))
                    msg.pop_back();

                logInfo("Multicast RX: '" + msg + "' from " + sender->address().to_string() + ":" + std::to_string(sender->port()));

                if (msg == "DISCOVER") {
                    std::string announce = "ANNOUNCE central " + std::to_string(port_);
                    boost::system::error_code se;
                    mcast_sock_->send_to(boost::asio::buffer(announce), *sender, 0, se);
                    if (se) logWarning("Multicast ANNOUNCE send_to error: " + se.message());
                    else    logInfo("Multicast TX: '" + announce + "'");
                }
            }
            if (mcast_sock_) startMulticastReceive_();
        }
    );
}

// ========================================================
// ====== Stubs / no-op helpers to avoid linker errors =====
// ========================================================

bool CentralServer::unregisterVehicleServer(const std::string& /*server_id*/) { return true; }
std::vector<std::string> CentralServer::getRegisteredVehicleServers() { return {}; }

bool CentralServer::registerRegionalServer(const std::string& /*server_id*/, const std::string& /*address*/, int /*port*/) { return true; }
bool CentralServer::syncWithRegionalServer(const std::string& /*server_id*/) { return true; }
void CentralServer::broadcastToRegionalServers(std::unique_ptr<Message> /*message*/) {}

bool CentralServer::updatePriceList(VehicleType /*vehicle_type*/, TicketType /*ticket_type*/, double /*price*/) { return true; }
bool CentralServer::updateVehicleCapacity(const std::string& /*uri*/, int /*capacity*/, int /*available_seats*/) { return true; }
void CentralServer::broadcastPriceUpdate() {}

bool CentralServer::processGroupCreation(const std::string& /*group_name*/, const std::string& /*leader_urn*/,
                                         const std::vector<std::string>& /*members*/) { return true; }
bool CentralServer::processUserDeletion(const std::string& urn, bool admin_approved) {
    // Politika: bez admin_approved -> NE brisati
    if (!admin_approved) {
        logInfo("User deletion requested WITHOUT admin approval for URN: " + urn);
        return false;
    }

    auto db = DatabasePool::getInstance().getConnection();
    if (!db) {
        logError("DB connection unavailable in processUserDeletion");
        return false;
    }

    // Mora postojati
    auto userPtr = db->getUser(urn);
    if (!userPtr) {
        DatabasePool::getInstance().returnConnection(db);
        logWarning("User not found for deletion: " + urn);
        return false;
    }

    bool ok = db->deleteUser(urn);
    const std::string dbErr = db->getLastError();
    DatabasePool::getInstance().returnConnection(db);

    if (!ok) {
        logError("Failed to delete user " + urn + (dbErr.empty() ? "" : (" | " + dbErr)));
        return false;
    }

    logInfo("User deleted with ADMIN approval: " + urn);
    return true;
}

bool CentralServer::processGroupMemberDeletion(int /*group_id*/, const std::string& /*member_urn*/,
                                               const std::string& /*leader_urn*/) { return true; }

bool CentralServer::processTicketPurchase(const std::string& /*user_urn*/, TicketType /*ticket_type*/,
                                          VehicleType /*vehicle_type*/, const std::string& /*route*/, int /*passengers*/) { return true; }

void CentralServer::processVehicleUpdate(const std::string& /*server_id*/, std::unique_ptr<Message> /*message*/) {}

std::map<std::string,int> CentralServer::getSystemStatistics() { return {}; }
std::vector<std::string> CentralServer::getActiveUsers() { return {}; }
std::map<VehicleType,int> CentralServer::getVehicleCapacityStatus() { return {}; }

bool CentralServer::connectToVehicleServer(VehicleServerInfo& /*server*/) { return true; }
void CentralServer::disconnectFromVehicleServer(const std::string& /*server_id*/) {}
void CentralServer::sendToVehicleServer(const std::string& /*server_id*/, std::unique_ptr<Message> /*message*/) {}

bool CentralServer::connectToRegionalServer(RegionalServerInfo& /*server*/) { return true; }
void CentralServer::sendToRegionalServer(const std::string& /*server_id*/, std::unique_ptr<Message> /*message*/) {}

std::string CentralServer::createSession(const std::string& user_urn, std::unique_ptr<TLSSocket> /*socket*/) {
    (void)user_urn;
    return generateSessionId();
}

bool CentralServer::validateSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    return client_sessions_.find(session_id) != client_sessions_.end();
}

void CentralServer::removeSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    client_sessions_.erase(session_id);
}

} // namespace transport

