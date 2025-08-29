#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <optional>    
#include <sqlite3.h>
#include "Message.h"

namespace transport {

// =========================
//  Database record structs
// =========================
struct User {
    std::string urn;
    std::string name;
    int         age;
    std::string registration_date;
    bool        active;
    std::string pin_hash;
};

struct Group {
    int                      group_id;
    std::string              group_name;
    std::string              leader_urn;
    std::vector<std::string> members;
    std::string              creation_date;
    bool                     active;
};

struct Vehicle {
    std::string uri;
    VehicleType type;
    int         capacity;
    int         available_seats;
    std::string route;
    bool        active;
    std::string last_update;
};

struct Ticket {
    std::string ticket_id;
    std::string user_urn;
    TicketType  type;
    VehicleType vehicle_type;
    std::string route;
    double      price;
    double      discount;
    std::string purchase_date;
    std::string seat_number;
    bool        used;
};

struct Payment {
    std::string transaction_id;
    std::string ticket_id;
    double      amount;
    std::string payment_method;
    std::string payment_date;
    bool        successful;
};

struct PriceList {
    VehicleType vehicle_type;
    TicketType  ticket_type;
    double      base_price;
    double      distance_multiplier;
    double      time_multiplier;
    std::string last_update;
};

// =========================
//       Database
// =========================
class Database {
public:
    Database();
    ~Database();

    bool initialize(const std::string& db_path);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    // Transactions
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Users
    bool                     registerUser(const User& user);
    bool                     updateUser(const User& user);
    bool                     deleteUser(const std::string& urn);
    std::unique_ptr<User>    getUser(const std::string& urn);
    std::vector<User>        getAllUsers();
    bool                     authenticateUser(const std::string& urn, const std::string& pin);

    // Groups (by id) 
    bool                  createGroup(const Group& group);
    bool                  updateGroup(const Group& group);
    bool                  deleteGroup(int group_id);
    bool                  addGroupMember(int group_id, const std::string& member_urn);
    bool                  removeGroupMember(int group_id, const std::string& member_urn);
    std::unique_ptr<Group> getGroup(int group_id);
    std::vector<Group>     getUserGroups(const std::string& urn);
    std::vector<Group>     getAllGroups();

    // Groups by name (used by CentralServer)
    std::string getGroupLeader(const std::string& group_name);
    bool        addUserToGroup(const std::string& urn, const std::string& group_name);
    bool        removeUserFromGroup(const std::string& urn, const std::string& group_name);

    // Vehicles
    bool                      registerVehicle(const Vehicle& vehicle);
    bool                      updateVehicle(const Vehicle& vehicle);
    bool                      deleteVehicle(const std::string& uri);
    std::unique_ptr<Vehicle>  getVehicle(const std::string& uri);
    std::vector<Vehicle>      getVehiclesByType(VehicleType type);
    std::vector<Vehicle>      getAllVehicles();
    bool                      updateSeatAvailability(const std::string& uri, int available_seats);
    std::unique_ptr<Vehicle>  getVehicleByRouteAndType(const std::string& route, VehicleType type);

    // Tickets
    bool                    createTicket(const Ticket& ticket);
    bool                    updateTicket(const Ticket& ticket);
    bool                    useTicket(const std::string& ticket_id);
    std::unique_ptr<Ticket> getTicket(const std::string& ticket_id);
    std::vector<Ticket>     getUserTickets(const std::string& urn);
    std::vector<Ticket>     getActiveTickets();

    // Payments
    bool                       recordPayment(const Payment& payment);
    bool                       updatePayment(const Payment& payment);
    std::unique_ptr<Payment>   getPayment(const std::string& transaction_id);
    std::vector<Payment>       getTicketPayments(const std::string& ticket_id);
    std::vector<Payment>       getUserPayments(const std::string& urn);

    // Prices
    bool                         updatePriceList(const PriceList& price);
    std::unique_ptr<PriceList>   getPrice(VehicleType vehicle_type, TicketType ticket_type);
    std::vector<PriceList>       getAllPrices();
    double                       calculateTicketPrice(VehicleType vehicle_type, TicketType ticket_type,
                                                      int passengers = 1, double distance = 1.0,
                                                      double time_minutes = 30.0);

    // Discounts (optional)
    double calculateDiscount(const std::string& urn, TicketType ticket_type, int group_size = 1);
    bool   isEligibleForAgeDiscount(const std::string& urn);
    bool   isEligibleForGroupDiscount(TicketType ticket_type, int group_size);

    // Stats (optional)
    std::map<std::string, int>    getVehicleUsageStats();
    std::map<std::string, double> getRevenueStats();
    std::vector<std::map<std::string, std::string>> getActiveConnections();

    // Maintenance (optional)
    bool        vacuum();
    bool        backup(const std::string& backup_path);
    bool        restore(const std::string& backup_path);
    std::string getDatabaseInfo();

    // ===== DODANO: admin helperi koje zove CentralServer =====
    bool updatePrice(VehicleType vehicle_type, TicketType ticket_type, double price);
    bool updateVehicle(const std::string& uri,
                       std::optional<bool> active = {},
                       std::optional<std::string> route = {},
                       std::optional<VehicleType> type = {});
    bool updateVehicleCapacity(const std::string& uri, int capacity, int available_seats);

    // Errors
    std::string getLastError() const { return last_error_; }
    int         getLastErrorCode() const { return last_error_code_; }

private:
    sqlite3*    db_{nullptr};
    std::mutex  db_mutex_;
    std::string last_error_;
    int         last_error_code_{0};

    // Internals
    bool createTables();
    bool executeSQL(const std::string& sql);
    bool prepareStatement(const std::string& sql, sqlite3_stmt** stmt);
    void setLastError(const std::string& error, int code = -1);
    std::string hashPassword(const std::string& password);
    bool verifyPassword(const std::string& password, const std::string& hash);

    // DDL helpers
    std::string getUsersTableSQL();
    std::string getGroupsTableSQL();
    std::string getGroupMembersTableSQL();
    std::string getVehiclesTableSQL();
    std::string getTicketsTableSQL();
    std::string getPaymentsTableSQL();
    std::string getPriceListTableSQL();
    std::string getActiveConnectionsTableSQL();

    // Row extractors
    User      extractUser(sqlite3_stmt* stmt);
    Group     extractGroup(sqlite3_stmt* stmt);
    Vehicle   extractVehicle(sqlite3_stmt* stmt);
    Ticket    extractTicket(sqlite3_stmt* stmt);
    Payment   extractPayment(sqlite3_stmt* stmt);
    PriceList extractPriceList(sqlite3_stmt* stmt);

    // Helpers
    int  getGroupIdByName(const std::string& group_name);
    bool userExists(const std::string& urn);
};

// =========================
//     DatabasePool
// =========================
class DatabasePool {
public:
    static DatabasePool& getInstance();

    bool                         initialize(const std::string& db_path, int pool_size = 5);
    std::shared_ptr<Database>    getConnection();
    void                         returnConnection(std::shared_ptr<Database> db);
    void                         shutdown();

private:
    std::vector<std::shared_ptr<Database>> connections_;
    std::vector<bool>                      available_;
    std::mutex                             pool_mutex_;
    std::condition_variable                pool_cv_;
    std::string                            db_path_;
    bool                                   initialized_{false};

    DatabasePool()  = default;
    ~DatabasePool() { shutdown(); }

    DatabasePool(const DatabasePool&)            = delete;
    DatabasePool& operator=(const DatabasePool&) = delete;
};

} // namespace transport

