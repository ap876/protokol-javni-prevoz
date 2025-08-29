#include "common/Database.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <algorithm>
#include <ctime>

namespace transport {

// ---------------------- mali helper za timestamp ----------------------
static inline std::string nowISO() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    tm = *std::localtime(&t);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// ======================== Database (lifecycle) ========================

Database::Database() : db_(nullptr), last_error_code_(0) {}

Database::~Database() {
    close();
}

bool Database::initialize(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    int result = sqlite3_open(db_path.c_str(), &db_);
    if (result != SQLITE_OK) {
        setLastError("Failed to open database: " + std::string(sqlite3_errmsg(db_)), result);
        return false;
    }

    // Enforcaj strane ključeve
    executeSQL("PRAGMA foreign_keys = ON;");
    // Omogući bolji paralelizam i čekanje na zaključavanja
    //executeSQL("PRAGMA journal_mode = WAL;");
    //executeSQL("PRAGMA synchronous = NORMAL;");
    //executeSQL("PRAGMA busy_timeout = 5000;"); // čekaj do 5s ako je DB busy

    return createTables();
}

void Database::close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// ======================== Schema ========================

bool Database::createTables() {
    std::vector<std::string> create_statements = {
        getUsersTableSQL(),
        getGroupsTableSQL(),
        getGroupMembersTableSQL(),
        getVehiclesTableSQL(),
        getTicketsTableSQL(),
        getPaymentsTableSQL(),
        getPriceListTableSQL(),
        getActiveConnectionsTableSQL()
    };

    for (const auto& sql : create_statements) {
        if (!executeSQL(sql)) {
            return false;
        }
    }
    return true;
}

bool Database::executeSQL(const std::string& sql) {
    char* error_msg = nullptr;
    int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);

    if (result != SQLITE_OK) {
        setLastError(error_msg ? error_msg : "Unknown SQL error", result);
        if (error_msg) sqlite3_free(error_msg);
        return false;
    }
    return true;
}

// ======================== Transactions (jednostavno) ========================

bool Database::beginTransaction()  { return executeSQL("BEGIN TRANSACTION;"); }
bool Database::commitTransaction() { return executeSQL("COMMIT;"); }
bool Database::rollbackTransaction(){ return executeSQL("ROLLBACK;"); }

// ======================== Users ========================

bool Database::registerUser(const User& user) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "INSERT INTO users (urn, name, age, registration_date, active, pin_hash) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, user.urn.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 3, user.age);
    sqlite3_bind_text(stmt, 4, user.registration_date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 5, user.active ? 1 : 0);
    sqlite3_bind_text(stmt, 6, user.pin_hash.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            setLastError("User already registered", rc);
        } else {
            setLastError("Failed to register user", rc);
        }
        return false;
    }
    return true;
}

std::unique_ptr<User> Database::getUser(const std::string& urn) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "SELECT urn, name, age, registration_date, active, pin_hash "
        "FROM users WHERE urn = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return nullptr;

    sqlite3_bind_text(stmt, 1, urn.c_str(), -1, SQLITE_STATIC);

    std::unique_ptr<User> user = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user = std::make_unique<User>(extractUser(stmt));
    }
    sqlite3_finalize(stmt);
    return user;
}

bool Database::authenticateUser(const std::string& urn, const std::string& pin) {
    auto user = getUser(urn);
    if (!user) return false;
    return verifyPassword(pin, user->pin_hash);
}

bool Database::updateUser(const User& user) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    const std::string sql =
        "UPDATE users SET name = ?, age = ?, registration_date = ?, active = ?, pin_hash = ? WHERE urn = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_text(stmt, 1, user.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, user.age);
    sqlite3_bind_text(stmt, 3, user.registration_date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 4, user.active ? 1 : 0);
    sqlite3_bind_text(stmt, 5, user.pin_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, user.urn.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to update user", rc);
        return false;
    }
    return true;
}

bool Database::deleteUser(const std::string& urn) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    const std::string sql = "DELETE FROM users WHERE urn = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_text(stmt, 1, urn.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to delete user", rc);
        return false;
    }
    return true;
}

std::vector<User> Database::getAllUsers() {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "SELECT urn, name, age, registration_date, active, pin_hash FROM users";

    sqlite3_stmt* stmt = nullptr;
    std::vector<User> out;

    if (!prepareStatement(sql, &stmt)) return out;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.emplace_back(extractUser(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

// ======================== Groups ========================

bool Database::createGroup(const Group& group) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    // 1) kreiraj grupu
    const std::string sql =
        "INSERT INTO groups (group_name, leader_urn, creation_date, active) VALUES (?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    const std::string creation = group.creation_date.empty() ? nowISO() : group.creation_date;

    sqlite3_bind_text(stmt, 1, group.group_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, group.leader_urn.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, creation.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 4, group.active ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to create group (is name unique?)", rc);
        return false;
    }

    // 2) dohvati id
    int group_id = -1;
    {
        const std::string q = "SELECT group_id FROM groups WHERE group_name = ? LIMIT 1";
        sqlite3_stmt* s2 = nullptr;
        if (!prepareStatement(q, &s2)) return false;
        sqlite3_bind_text(s2, 1, group.group_name.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(s2) == SQLITE_ROW) {
            group_id = sqlite3_column_int(s2, 0);
        }
        sqlite3_finalize(s2);
    }
    if (group_id < 0) {
        setLastError("Failed to resolve new group_id", -1);
        return false;
    }

    // 3) dodaj lidera kao člana
    const std::string sqlAdd =
        "INSERT OR REPLACE INTO group_members (group_id, member_urn, join_date, active) VALUES (?, ?, ?, 1)";
    sqlite3_stmt* s3 = nullptr;
    if (!prepareStatement(sqlAdd, &s3)) return false;
    sqlite3_bind_int (s3, 1, group_id);
    sqlite3_bind_text(s3, 2, group.leader_urn.c_str(), -1, SQLITE_STATIC);
    std::string jd = nowISO();
    sqlite3_bind_text(s3, 3, jd.c_str(), -1, SQLITE_STATIC);
    int r3 = sqlite3_step(s3);
    sqlite3_finalize(s3);
    if (r3 != SQLITE_DONE) {
        setLastError("Failed to add leader as group member", r3);
        return false;
    }

    return true;
}

int Database::getGroupIdByName(const std::string& group_name) {
    const std::string sql =
        "SELECT group_id FROM groups WHERE group_name = ? AND active = 1 LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return -1;

    sqlite3_bind_text(stmt, 1, group_name.c_str(), -1, SQLITE_STATIC);

    int gid = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        gid = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return gid;
}

bool Database::userExists(const std::string& urn) {
    const std::string sql = "SELECT 1 FROM users WHERE urn = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;
    sqlite3_bind_text(stmt, 1, urn.c_str(), -1, SQLITE_STATIC);
    bool ok = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return ok;
}

std::string Database::getGroupLeader(const std::string& group_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    const std::string sql =
        "SELECT leader_urn FROM groups WHERE group_name = ? AND active = 1 LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return "";

    sqlite3_bind_text(stmt, 1, group_name.c_str(), -1, SQLITE_STATIC);

    std::string leader;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* c0 = sqlite3_column_text(stmt, 0);
        leader = c0 ? reinterpret_cast<const char*>(c0) : "";
    }
    sqlite3_finalize(stmt);
    return leader;
}

bool Database::addUserToGroup(const std::string& urn, const std::string& group_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear();
    last_error_code_ = 0;

    if (!userExists(urn)) {
        setLastError("User not found", SQLITE_NOTFOUND);
        return false;
    }
    int gid = getGroupIdByName(group_name);
    if (gid <= 0) {
        setLastError("Group not found", SQLITE_NOTFOUND);
        return false;
    }

    // Provjeri postoji li već
    sqlite3_stmt* chk = nullptr;
    if (!prepareStatement("SELECT active FROM group_members WHERE group_id=? AND member_urn=? LIMIT 1", &chk))
        return false;

    sqlite3_bind_int (chk, 1, gid);
    sqlite3_bind_text(chk, 2, urn.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(chk);
    if (rc == SQLITE_ROW) {
        int active = sqlite3_column_int(chk, 0);
        sqlite3_finalize(chk);
        if (active != 0) {
            // već aktivan član -> odbij
            setLastError("User already in group", SQLITE_CONSTRAINT);
            return false;
        }
        // postoji ali neaktivan -> reaktiviraj
        sqlite3_stmt* upd = nullptr;
        if (!prepareStatement("UPDATE group_members SET active=1, join_date=? WHERE group_id=? AND member_urn=?", &upd))
            return false;

        std::string jd = nowISO();
        sqlite3_bind_text(upd, 1, jd.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (upd, 2, gid);
        sqlite3_bind_text(upd, 3, urn.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(upd);
        int changes = sqlite3_changes(db_);
        sqlite3_finalize(upd);

        if (rc != SQLITE_DONE || changes != 1) {
            setLastError("Failed to reactivate user in group", rc);
            return false;
        }
        return true;
    }
    sqlite3_finalize(chk);

    // Novi član — INSERT
    sqlite3_stmt* ins = nullptr;
    if (!prepareStatement("INSERT INTO group_members (group_id, member_urn, join_date, active) VALUES (?,?,?,1)", &ins))
        return false;

    std::string jd = nowISO();
    sqlite3_bind_int (ins, 1, gid);
    sqlite3_bind_text(ins, 2, urn.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, jd.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(ins);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(ins);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to add user to group", rc);
        return false;
    }
    return changes == 1;
}

bool Database::removeUserFromGroup(const std::string& urn, const std::string& group_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear();
    last_error_code_ = 0;

    int gid = getGroupIdByName(group_name);
    if (gid <= 0) {
        setLastError("Group not found", SQLITE_NOTFOUND);
        return false;
    }

    const std::string sql =
        "DELETE FROM group_members WHERE group_id = ? AND member_urn = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_int (stmt, 1, gid);
    sqlite3_bind_text(stmt, 2, urn.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to remove user from group", rc);
        return false;
    }
    // ako nije obrisao ništa -> false
    if (changes == 0) {
        setLastError("User not in group", SQLITE_NOTFOUND);
        return false;
    }
    return true;
}

// ======================== Vehicles / Tickets / Payments ========================

bool Database::registerVehicle(const Vehicle& vehicle) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "INSERT OR REPLACE INTO vehicles (uri, type, capacity, available_seats, route, active, last_update) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_text(stmt, 1, vehicle.uri.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, static_cast<int>(vehicle.type));
    sqlite3_bind_int (stmt, 3, vehicle.capacity);
    sqlite3_bind_int (stmt, 4, vehicle.available_seats);
    sqlite3_bind_text(stmt, 5, vehicle.route.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 6, vehicle.active ? 1 : 0);
    sqlite3_bind_text(stmt, 7, vehicle.last_update.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to register vehicle", rc);
        return false;
    }
    return true;
}

std::unique_ptr<Vehicle> Database::getVehicle(const std::string& uri) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "SELECT uri, type, capacity, available_seats, route, active, last_update "
        "FROM vehicles WHERE uri = ? LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return nullptr;

    sqlite3_bind_text(stmt, 1, uri.c_str(), -1, SQLITE_STATIC);

    std::unique_ptr<Vehicle> vehicle = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Vehicle v = extractVehicle(stmt);
        vehicle = std::make_unique<Vehicle>(v);
    }
    sqlite3_finalize(stmt);
    return vehicle;
}

bool Database::updateSeatAvailability(const std::string& uri, int available_seats) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql = "UPDATE vehicles SET available_seats = ? WHERE uri = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_int (stmt, 1, available_seats);
    sqlite3_bind_text(stmt, 2, uri.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to update available seats", rc);
        return false;
    }
    return true;
}

bool Database::createTicket(const Ticket& ticket) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "INSERT INTO tickets (ticket_id, user_urn, type, vehicle_type, route, price, discount, purchase_date, seat_number, used) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_text(stmt, 1, ticket.ticket_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ticket.user_urn.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 3, static_cast<int>(ticket.type));
    sqlite3_bind_int (stmt, 4, static_cast<int>(ticket.vehicle_type));
    sqlite3_bind_text(stmt, 5, ticket.route.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, ticket.price);
    sqlite3_bind_double(stmt, 7, ticket.discount);
    sqlite3_bind_text(stmt, 8, ticket.purchase_date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, ticket.seat_number.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 10, ticket.used ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to insert ticket", rc);
        return false;
    }
    return true;
}

bool Database::recordPayment(const Payment& payment) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "INSERT INTO payments (transaction_id, ticket_id, amount, payment_method, payment_date, successful) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    sqlite3_bind_text(stmt, 1, payment.transaction_id.c_str(), -1, SQLITE_STATIC);

    if (payment.ticket_id.empty()) sqlite3_bind_null(stmt, 2);
    else                           sqlite3_bind_text(stmt, 2, payment.ticket_id.c_str(), -1, SQLITE_STATIC);

    sqlite3_bind_double(stmt, 3, payment.amount);
    sqlite3_bind_text  (stmt, 4, payment.payment_method.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text  (stmt, 5, payment.payment_date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int   (stmt, 6, payment.successful ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        setLastError("Failed to insert payment", rc);
        return false;
    }
    return true;
}

double Database::calculateTicketPrice(VehicleType /*vehicle_type*/,
                                      TicketType  /*ticket_type*/,
                                      int         /*passengers*/,
                                      double      /*distance*/,
                                      double      /*time_minutes*/) {
    // Placeholder – vrati baznu cijenu 1.0
    return 1.0;
}
std::unique_ptr<PriceList> Database::getPrice(VehicleType vehicle_type, TicketType ticket_type) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear(); 
    last_error_code_ = 0;

    const std::string sql =
        "SELECT vehicle_type, ticket_type, base_price, distance_multiplier, time_multiplier, last_update "
        "FROM price_list WHERE vehicle_type = ? AND ticket_type = ? LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return nullptr;

    sqlite3_bind_int(stmt, 1, static_cast<int>(vehicle_type));
    sqlite3_bind_int(stmt, 2, static_cast<int>(ticket_type));

    std::unique_ptr<PriceList> out = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        PriceList p = extractPriceList(stmt);
        out = std::make_unique<PriceList>(p);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Database::updatePriceList(const PriceList& price) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear(); 
    last_error_code_ = 0;

    // 1) Pokušaj UPDATE
    const std::string sql_upd =
        "UPDATE price_list SET base_price = ?, distance_multiplier = ?, time_multiplier = ?, last_update = ? "
        "WHERE vehicle_type = ? AND ticket_type = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql_upd, &stmt)) return false;

    const std::string ts = price.last_update.empty() ? nowISO() : price.last_update;

    sqlite3_bind_double(stmt, 1, price.base_price);
    sqlite3_bind_double(stmt, 2, price.distance_multiplier);
    sqlite3_bind_double(stmt, 3, price.time_multiplier);
    sqlite3_bind_text  (stmt, 4, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt, 5, static_cast<int>(price.vehicle_type));
    sqlite3_bind_int   (stmt, 6, static_cast<int>(price.ticket_type));

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to update price_list", rc);
        return false;
    }
    if (changes > 0) return true;

    // 2) Ako UPDATE nije ništa izmijenio → INSERT
    const std::string sql_ins =
        "INSERT INTO price_list (vehicle_type, ticket_type, base_price, distance_multiplier, time_multiplier, last_update) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* ins = nullptr;
    if (!prepareStatement(sql_ins, &ins)) return false;

    sqlite3_bind_int   (ins, 1, static_cast<int>(price.vehicle_type));
    sqlite3_bind_int   (ins, 2, static_cast<int>(price.ticket_type));
    sqlite3_bind_double(ins, 3, price.base_price);
    sqlite3_bind_double(ins, 4, price.distance_multiplier);
    sqlite3_bind_double(ins, 5, price.time_multiplier);
    sqlite3_bind_text  (ins, 6, ts.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(ins);
    sqlite3_finalize(ins);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to insert into price_list", rc);
        return false;
    }
    return true;
}

// ---- DISCOUNT HELPERS ----
double Database::calculateDiscount(const std::string& /*urn*/, TicketType ticket_type, int group_size) {
    // Pravila:
    // - Family paket -> 10%
    // - Ili ako je kupljeno 3 ili više karata -> 10%
    if (ticket_type == TicketType::GROUP_FAMILY) return 0.10;
    if (group_size >= 3) return 0.10;
    return 0.0;
}

bool Database::isEligibleForAgeDiscount(const std::string& /*urn*/) {
    // Nije implementirano (placeholder); vrati false
    return false;
}

bool Database::isEligibleForGroupDiscount(TicketType ticket_type, int group_size) {
    if (ticket_type == TicketType::GROUP_FAMILY) return true;
    if (group_size >= 3) return true;
    return false;
}

std::unique_ptr<Vehicle> Database::getVehicleByRouteAndType(const std::string& route, VehicleType type) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    const std::string sql =
        "SELECT uri, type, capacity, available_seats, route, active, last_update "
        "FROM vehicles WHERE route = ? AND type = ? LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return nullptr;

    sqlite3_bind_text(stmt, 1, route.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, static_cast<int>(type));

    std::unique_ptr<Vehicle> vehicle = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Vehicle v = extractVehicle(stmt);
        vehicle = std::make_unique<Vehicle>(v);
    }
    sqlite3_finalize(stmt);
    return vehicle;
}

// ======================== Helpers (prepare, hash, extract) ========================

bool Database::prepareStatement(const std::string& sql, sqlite3_stmt** stmt) {
    int result = sqlite3_prepare_v2(db_, sql.c_str(), -1, stmt, nullptr);
    if (result != SQLITE_OK) {
        setLastError("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)), result);
        return false;
    }
    return true;
}

void Database::setLastError(const std::string& error, int code) {
    last_error_      = error;
    last_error_code_ = code;
}

std::string Database::hashPassword(const std::string& password) {
    std::hash<std::string> hasher;
    return std::to_string(hasher(password + "salt"));
}

bool Database::verifyPassword(const std::string& password, const std::string& hash) {
    return hashPassword(password) == hash;
}

User Database::extractUser(sqlite3_stmt* stmt) {
    User u{};
    const unsigned char* c0 = sqlite3_column_text(stmt, 0);
    const unsigned char* c1 = sqlite3_column_text(stmt, 1);
    const unsigned char* c3 = sqlite3_column_text(stmt, 3);
    const unsigned char* c5 = sqlite3_column_text(stmt, 5);

    u.urn               = c0 ? reinterpret_cast<const char*>(c0) : "";
    u.name              = c1 ? reinterpret_cast<const char*>(c1) : "";
    u.age               = sqlite3_column_int(stmt, 2);
    u.registration_date = c3 ? reinterpret_cast<const char*>(c3) : "";
    u.active            = sqlite3_column_int(stmt, 4) != 0;
    u.pin_hash          = c5 ? reinterpret_cast<const char*>(c5) : "";
    return u;
}

Vehicle Database::extractVehicle(sqlite3_stmt* stmt) {
    Vehicle v{};
    const unsigned char* c0 = sqlite3_column_text(stmt, 0);
    const unsigned char* c4 = sqlite3_column_text(stmt, 4);
    const unsigned char* c6 = sqlite3_column_text(stmt, 6);

    v.uri             = c0 ? reinterpret_cast<const char*>(c0) : "";
    v.type            = static_cast<VehicleType>(sqlite3_column_int(stmt, 1));
    v.capacity        = sqlite3_column_int(stmt, 2);
    v.available_seats = sqlite3_column_int(stmt, 3);
    v.route           = c4 ? reinterpret_cast<const char*>(c4) : "";
    v.active          = sqlite3_column_int(stmt, 5) != 0;
    v.last_update     = c6 ? reinterpret_cast<const char*>(c6) : "";
    return v;
}

Group Database::extractGroup(sqlite3_stmt* stmt) {
    Group g{};
    const unsigned char* c1 = sqlite3_column_text(stmt, 1);
    const unsigned char* c2 = sqlite3_column_text(stmt, 2);
    const unsigned char* c3 = sqlite3_column_text(stmt, 3);

    g.group_id      = sqlite3_column_int(stmt, 0);
    g.group_name    = c1 ? reinterpret_cast<const char*>(c1) : "";
    g.leader_urn    = c2 ? reinterpret_cast<const char*>(c2) : "";
    g.creation_date = c3 ? reinterpret_cast<const char*>(c3) : "";
    g.active        = sqlite3_column_int(stmt, 4) != 0;
    return g;
}

Ticket Database::extractTicket(sqlite3_stmt* stmt) {
    Ticket t{};
    const unsigned char* c0 = sqlite3_column_text(stmt, 0);
    const unsigned char* c1 = sqlite3_column_text(stmt, 1);
    const unsigned char* c4 = sqlite3_column_text(stmt, 4);
    const unsigned char* c7 = sqlite3_column_text(stmt, 7);
    const unsigned char* c8 = sqlite3_column_text(stmt, 8);

    t.ticket_id     = c0 ? reinterpret_cast<const char*>(c0) : "";
    t.user_urn      = c1 ? reinterpret_cast<const char*>(c1) : "";
    t.type          = static_cast<TicketType>(sqlite3_column_int(stmt, 2));
    t.vehicle_type  = static_cast<VehicleType>(sqlite3_column_int(stmt, 3));
    t.route         = c4 ? reinterpret_cast<const char*>(c4) : "";
    t.price         = sqlite3_column_double(stmt, 5);
    t.discount      = sqlite3_column_double(stmt, 6);
    t.purchase_date = c7 ? reinterpret_cast<const char*>(c7) : "";
    t.seat_number   = c8 ? reinterpret_cast<const char*>(c8) : "";
    t.used          = sqlite3_column_int(stmt, 9) != 0;
    return t;
}

Payment Database::extractPayment(sqlite3_stmt* stmt) {
    Payment p{};
    const unsigned char* c0 = sqlite3_column_text(stmt, 0);
    const unsigned char* c1 = sqlite3_column_text(stmt, 1);
    const unsigned char* c3 = sqlite3_column_text(stmt, 3);
    const unsigned char* c4 = sqlite3_column_text(stmt, 4);

    p.transaction_id = c0 ? reinterpret_cast<const char*>(c0) : "";
    p.ticket_id      = c1 ? reinterpret_cast<const char*>(c1) : "";
    p.amount         = sqlite3_column_double(stmt, 2);
    p.payment_method = c3 ? reinterpret_cast<const char*>(c3) : "";
    p.payment_date   = c4 ? reinterpret_cast<const char*>(c4) : "";
    p.successful     = sqlite3_column_int(stmt, 5) != 0;
    return p;
}

PriceList Database::extractPriceList(sqlite3_stmt* stmt) {
    // Redoslijed kolona odgovara SELECT-u u getPrice():
    // 0: vehicle_type, 1: ticket_type, 2: base_price,
    // 3: distance_multiplier, 4: time_multiplier, 5: last_update
    PriceList p{};
    p.vehicle_type        = static_cast<VehicleType>(sqlite3_column_int(stmt, 0));
    p.ticket_type         = static_cast<TicketType>(sqlite3_column_int(stmt, 1));
    p.base_price          = sqlite3_column_double(stmt, 2);
    p.distance_multiplier = sqlite3_column_double(stmt, 3);
    p.time_multiplier     = sqlite3_column_double(stmt, 4);
    const unsigned char* c5 = sqlite3_column_text(stmt, 5);
    p.last_update         = c5 ? reinterpret_cast<const char*>(c5) : "";
    return p;
}

// ======================== SQL create strings ========================

std::string Database::getUsersTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS users (
            urn TEXT PRIMARY KEY,
            name TEXT,
            age INTEGER,
            registration_date TEXT,
            active BOOLEAN,
            pin_hash TEXT
        )
    )";
}

std::string Database::getGroupsTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS groups (
            group_id INTEGER PRIMARY KEY AUTOINCREMENT,
            group_name TEXT UNIQUE,
            leader_urn TEXT,
            creation_date TEXT,
            active BOOLEAN,
            FOREIGN KEY (leader_urn) REFERENCES users(urn)
        )
    )";
}

std::string Database::getGroupMembersTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS group_members (
            group_id INTEGER,
            member_urn TEXT,
            join_date TEXT,
            active BOOLEAN,
            PRIMARY KEY (group_id, member_urn),
            FOREIGN KEY (group_id) REFERENCES groups(group_id) ON DELETE CASCADE,
            FOREIGN KEY (member_urn) REFERENCES users(urn)
        )
    )";
}

std::string Database::getVehiclesTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS vehicles (
            uri TEXT PRIMARY KEY,
            type INTEGER,
            capacity INTEGER,
            available_seats INTEGER,
            route TEXT,
            active BOOLEAN,
            last_update TEXT
        )
    )";
}

std::string Database::getTicketsTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS tickets (
            ticket_id TEXT PRIMARY KEY,
            user_urn TEXT,
            type INTEGER,
            vehicle_type INTEGER,
            route TEXT,
            price REAL,
            discount REAL,
            purchase_date TEXT,
            seat_number TEXT,
            used BOOLEAN,
            FOREIGN KEY (user_urn) REFERENCES users(urn)
        )
    )";
}

std::string Database::getPaymentsTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS payments (
            transaction_id TEXT PRIMARY KEY,
            ticket_id TEXT,
            amount REAL,
            payment_method TEXT,
            payment_date TEXT,
            successful BOOLEAN,
            FOREIGN KEY (ticket_id) REFERENCES tickets(ticket_id)
        )
    )";
}

std::string Database::getPriceListTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS price_list (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vehicle_type INTEGER,
            ticket_type INTEGER,
            base_price REAL,
            distance_multiplier REAL,
            time_multiplier REAL,
            last_update TEXT
        )
    )";
}

std::string Database::getActiveConnectionsTableSQL() {
    return R"(
        CREATE TABLE IF NOT EXISTS active_connections (
            connection_id TEXT PRIMARY KEY,
            client_address TEXT,
            client_port INTEGER,
            user_urn TEXT,
            connect_time TEXT,
            last_activity TEXT,
            authenticated BOOLEAN,
            FOREIGN KEY (user_urn) REFERENCES users(urn)
        )
    )";
}

// ======================== DODANO: Admin update helperi ========================

bool Database::updatePrice(VehicleType vehicle_type, TicketType ticket_type, double price) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear(); last_error_code_ = 0;

    const char* updSql =
        "UPDATE price_list SET base_price=?, last_update=? "
        "WHERE vehicle_type=? AND ticket_type=?";

    sqlite3_stmt* upd = nullptr;
    if (!prepareStatement(updSql, &upd)) return false;

    std::string ts = nowISO();
    sqlite3_bind_double(upd, 1, price);
    sqlite3_bind_text  (upd, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (upd, 3, static_cast<int>(vehicle_type));
    sqlite3_bind_int   (upd, 4, static_cast<int>(ticket_type));

    int rc = sqlite3_step(upd);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(upd);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to update price_list", rc);
        return false;
    }
    if (changes > 0) return true;

    // nije postojalo – INSERT
    const char* insSql =
        "INSERT INTO price_list (vehicle_type, ticket_type, base_price, distance_multiplier, time_multiplier, last_update) "
        "VALUES (?, ?, ?, 1.0, 1.0, ?)";
    sqlite3_stmt* ins = nullptr;
    if (!prepareStatement(insSql, &ins)) return false;

    sqlite3_bind_int   (ins, 1, static_cast<int>(vehicle_type));
    sqlite3_bind_int   (ins, 2, static_cast<int>(ticket_type));
    sqlite3_bind_double(ins, 3, price);
    sqlite3_bind_text  (ins, 4, ts.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(ins);
    sqlite3_finalize(ins);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to insert into price_list", rc);
        return false;
    }
    return true;
}

bool Database::updateVehicle(const std::string& uri,
                             std::optional<bool> active,
                             std::optional<std::string> route,
                             std::optional<VehicleType> type) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear(); last_error_code_ = 0;

    if (uri.empty()) { setLastError("Empty URI", SQLITE_MISUSE); return false; }
    if (!active.has_value() && !route.has_value() && !type.has_value()) {
        setLastError("Nothing to update", SQLITE_MISUSE);
        return false;
    }

    std::string sql = "UPDATE vehicles SET ";
    bool first = true;
    auto addField = [&](const char* f){ if (!first) sql += ", "; sql += f; first = false; };

    if (active.has_value()) addField("active = ?");
    if (route.has_value())  addField("route = ?");
    if (type.has_value())   addField("type = ?");
    addField("last_update = ?");
    sql += " WHERE uri = ?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    int idx = 1;
    if (active.has_value()) sqlite3_bind_int (stmt, idx++, *active ? 1 : 0);
    if (route.has_value())  sqlite3_bind_text(stmt, idx++, route->c_str(), -1, SQLITE_TRANSIENT);
    if (type.has_value())   sqlite3_bind_int (stmt, idx++, static_cast<int>(*type));

    std::string ts = nowISO();
    sqlite3_bind_text(stmt, idx++, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, uri.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to update vehicle", rc);
        return false;
    }
    if (changes == 0) {
        setLastError("Vehicle not found", SQLITE_NOTFOUND);
        return false;
    }
    return true;
}

bool Database::updateVehicleCapacity(const std::string& uri, int capacity, int available_seats) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    last_error_.clear(); last_error_code_ = 0;

    if (uri.empty()) { setLastError("Empty URI", SQLITE_MISUSE); return false; }
    if (capacity < 0 || available_seats < 0 || available_seats > capacity) {
        setLastError("Invalid capacity/available_seats", SQLITE_MISUSE);
        return false;
    }

    const char* sql =
        "UPDATE vehicles SET capacity=?, available_seats=?, last_update=? WHERE uri=?";

    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;

    std::string ts = nowISO();
    sqlite3_bind_int (stmt, 1, capacity);
    sqlite3_bind_int (stmt, 2, available_seats);
    sqlite3_bind_text(stmt, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, uri.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        setLastError("Failed to update vehicle capacity", rc);
        return false;
    }
    if (changes == 0) {
        setLastError("Vehicle not found", SQLITE_NOTFOUND);
        return false;
    }
    return true;
}

// ======================== DatabasePool ========================

DatabasePool& DatabasePool::getInstance() {
    static DatabasePool instance;
    return instance;
}

bool DatabasePool::initialize(const std::string& db_path, int pool_size) {
    db_path_ = db_path;
    connections_.resize(pool_size);
    available_.resize(pool_size, true);

    for (int i = 0; i < pool_size; ++i) {
        connections_[i] = std::make_shared<Database>();
        if (!connections_[i]->initialize(db_path)) {
            return false;
        }
    }

    initialized_ = true;
    return true;
}

std::shared_ptr<Database> DatabasePool::getConnection() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    pool_cv_.wait(lock, [this] {
        for (bool avail : available_) if (avail) return true;
        return false;
    });

    for (size_t i = 0; i < available_.size(); ++i) {
        if (available_[i]) {
            available_[i] = false;
            return connections_[i];
        }
    }
    return nullptr;
}

void DatabasePool::returnConnection(std::shared_ptr<Database> db) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i] == db) {
            available_[i] = true;
            pool_cv_.notify_one();
            break;
        }
    }
}

void DatabasePool::shutdown() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    connections_.clear();
    available_.clear();
    initialized_ = false;
}

} // namespace transport

