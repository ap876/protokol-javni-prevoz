#pragma once

#include "ServerBase.h"
#include "../common/Database.h"
#include "../common/TLSSocket.h"

#include <map>
#include <set>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include <chrono>
#include <memory>
#include <string>

// Boost.Asio samo za UDP multicast (DISCOVER/ANNOUNCE)
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>

namespace transport {

// Central Server - Main coordination and data aggregation server
class CentralServer : public ServerBase {
public:
    CentralServer();
    ~CentralServer();

    bool start(int port, const std::string& config_file = "");
    void stop() override;
    
    // Server configuration
    bool loadConfiguration(const std::string& config_file);
    void setDatabasePath(const std::string& db_path) { db_path_ = db_path; }
    void setCertificatePath(const std::string& cert_path, const std::string& key_path);

    // Multicast (CLI-friendly seteri)
    void setMulticastEnabled(bool on) { config_.enable_multicast = on; }
    void setMulticastAddress(const std::string& addr) { config_.multicast_address = addr; }
    void setMulticastPort(int port) { config_.multicast_port = port; }

    // Vehicle server registration
    bool registerVehicleServer(const std::string& server_id, VehicleType type, 
                               const std::string& address, int port);
    bool unregisterVehicleServer(const std::string& server_id);
    std::vector<std::string> getRegisteredVehicleServers();

    // Regional server communication
    bool registerRegionalServer(const std::string& server_id, const std::string& address, int port);
    bool syncWithRegionalServer(const std::string& server_id);
    void broadcastToRegionalServers(std::unique_ptr<Message> message);

    // Price list management (javne administrativne operacije)
    bool updatePriceList(VehicleType vehicle_type, TicketType ticket_type, double price);
    bool updateVehicleCapacity(const std::string& uri, int capacity, int available_seats);
    void broadcastPriceUpdate();

    // User and group management
    bool processUserRegistration(const std::string& urn);
    bool processGroupCreation(const std::string& group_name, const std::string& leader_urn, 
                              const std::vector<std::string>& members);
    bool processUserDeletion(const std::string& urn, bool admin_approved = false);
    bool processGroupMemberDeletion(int group_id, const std::string& member_urn, 
                                    const std::string& leader_urn);

    // Seat reservation and ticket processing
    bool processSeatReservation(const std::string& user_urn, VehicleType vehicle_type, 
                                const std::string& route);
    bool processTicketPurchase(const std::string& user_urn, TicketType ticket_type, 
                               VehicleType vehicle_type, const std::string& route, 
                               int passengers = 1);

    // Data aggregation from vehicle servers
    void collectVehicleData();
    void processVehicleUpdate(const std::string& server_id, std::unique_ptr<Message> message);

    // Statistics and monitoring
    std::map<std::string, int> getSystemStatistics();
    std::vector<std::string> getActiveUsers();
    std::map<VehicleType, int> getVehicleCapacityStatus();

    // Multicast communication (limited use as per requirements)
    void sendMulticastUpdate(const std::string& update_type, 
                             const std::map<std::string, std::string>& data);

protected:
    void handleClientMessage(std::unique_ptr<TLSSocket> client, 
                             std::unique_ptr<Message> message) override;
    void processMessage(std::unique_ptr<Message> message, 
                        std::unique_ptr<TLSSocket>& client) override;

private:
    struct VehicleServerInfo {
        std::string server_id;
        VehicleType type;
        std::string address;
        int port;
        bool active{false};
        std::chrono::system_clock::time_point last_heartbeat{};
        std::unique_ptr<TLSSocket> connection;
    };

    struct RegionalServerInfo {
        std::string server_id;
        std::string address;
        int port;
        bool active{false};
        std::unique_ptr<TLSSocket> connection;
    };

    struct ClientSession {
        std::string session_id;
        std::string user_urn;
        bool authenticated{false};
        std::chrono::system_clock::time_point last_activity{};
        std::unique_ptr<TLSSocket> socket;
    };

    // Server data
    std::string db_path_;
    std::shared_ptr<Database> database_;
    
    // Connected servers
    std::map<std::string, VehicleServerInfo> vehicle_servers_;
    std::map<std::string, RegionalServerInfo> regional_servers_;
    std::mutex servers_mutex_;

    // Client sessions
    std::map<std::string, ClientSession> client_sessions_;
    std::mutex sessions_mutex_;

    // Background tasks
    std::unique_ptr<std::thread> data_collection_thread_;
    std::unique_ptr<std::thread> heartbeat_thread_;
    std::unique_ptr<std::thread> cleanup_thread_;
    std::atomic<bool> background_running_{false};

    // Multicast subscribers (live client sockets)
    std::mutex broadcast_mutex_;
    std::vector<TLSSocket*> subscribers_;

    // Configuration
    struct Config {
        int max_connections = 1000;
        int heartbeat_interval = 30;       // seconds
        int session_timeout = 3600;        // seconds
        int data_collection_interval = 60; // seconds
        bool enable_multicast = false;
        std::string multicast_address = "239.192.0.1"; // administrativni opseg
        int multicast_port = 30001;
    } config_;

    // Internal methods
    bool initializeDatabase();
    void startBackgroundTasks();
    void stopBackgroundTasks();
    
    // Message handling methods
    void handleConnectRequest(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleAuthRequest(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleUserRegistration(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleDeviceRegistration(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleSeatReservation(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleTicketPurchase(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleGroupCreation(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleUserDeletion(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);

    // NEW: group membership handlers
    void handleAddMemberToGroup(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleRemoveMemberFromGroup(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);

    // NEW: admin update handlers (cjenovnik/vozila/kapacitet)
    void handleUpdatePrice(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleUpdateVehicle(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);
    void handleUpdateCapacity(std::unique_ptr<Message> message, std::unique_ptr<TLSSocket>& client);

    // Background task methods
    void dataCollectionLoop();
    void heartbeatLoop();
    void sessionCleanupLoop();

    // Vehicle server communication
    bool connectToVehicleServer(VehicleServerInfo& server);
    void disconnectFromVehicleServer(const std::string& server_id);
    void sendToVehicleServer(const std::string& server_id, std::unique_ptr<Message> message);

    // Regional server communication
    bool connectToRegionalServer(RegionalServerInfo& server);
    void sendToRegionalServer(const std::string& server_id, std::unique_ptr<Message> message);

    // Session management
    std::string createSession(const std::string& user_urn, std::unique_ptr<TLSSocket> socket);
    bool validateSession(const std::string& session_id);
    void removeSession(const std::string& session_id);
    void cleanupExpiredSessions();

    // Utility methods
    std::string generateSessionId();
    std::string generateTicketId();
    std::string generateTransactionId();
    std::string getCurrentTimestamp();
    bool        validateURN(const std::string& urn);
    bool        validateURI(const std::string& uri);

    // NEW: leader check helper
    bool requireGroupLeader(const std::string& session_id,
                            const std::string& group_name,
                            std::unique_ptr<TLSSocket>& client);

    // Discount calculation
    double calculateFinalPrice(const std::string& user_urn, TicketType ticket_type, 
                               VehicleType vehicle_type, int passengers);
    
    // --- UDP multicast (Boost.Asio) ---
    bool setupMulticast();
    void cleanupMulticast();
    void startMulticastReceive_();

    std::unique_ptr<boost::asio::io_context>      mcast_io_;
    std::unique_ptr<std::thread>                  mcast_thread_;
    std::unique_ptr<boost::asio::ip::udp::socket> mcast_sock_;
    boost::asio::ip::udp::endpoint                mcast_group_;
    boost::asio::ip::udp::endpoint                mcast_listen_ep_;
    std::array<char, 512>                         mcast_rx_buf_{};
};

} // namespace transport

