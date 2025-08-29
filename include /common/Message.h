#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <memory>
#include <optional>  // za opcione parametre u factory metodama

namespace transport {

// =========================
// Tipovi poruka (MessageType)
// =========================
enum class MessageType : uint16_t {
    CONNECT_REQUEST      = 1,
    CONNECT_RESPONSE     = 2,
    AUTH_REQUEST         = 3,
    AUTH_RESPONSE        = 4,
    REGISTER_USER        = 5,
    REGISTER_DEVICE      = 6,
    RESERVE_SEAT         = 7,
    PURCHASE_TICKET      = 8,
    CREATE_GROUP         = 9,
    DELETE_USER          = 10,
    DELETE_GROUP_MEMBER  = 11,     // remove member (samo lider)
    UPDATE_PRICE_LIST    = 12,    
    GET_VEHICLE_STATUS   = 13,
    MULTICAST_UPDATE     = 14,
    RESPONSE_SUCCESS     = 15,
    RESPONSE_ERROR       = 16,
    HEARTBEAT            = 17,
    DISCONNECT           = 18,

    // NOVO — eksplicitni "admin update" tipovi koje koristi CentralServer:
    UPDATE_PRICE         = 19,     // ažuriranje cijene (vehicle_type, ticket_type, price)
    UPDATE_VEHICLE       = 20,     // ažuriranje vozila (active/route/type)
    UPDATE_CAPACITY      = 21,     // ažuriranje kapaciteta (capacity/available_seats)

    // NEW:
    ADD_MEMBER_TO_GROUP  = 1001    // add member (bilo koji ulogovani korisnik)
};

// =========================
// Tipovi vozila / karata
// =========================
enum class VehicleType : uint8_t {
    BUS        = 1,
    TRAM       = 2,
    TROLLEYBUS = 3
};

enum class TicketType : uint8_t {
    INDIVIDUAL     = 1,
    GROUP_FAMILY   = 2,
    GROUP_BUSINESS = 3,
    GROUP_TOURIST  = 4
};

// =========================
// Klasa Message (format okvira)
// =========================
class Message {
public:
    struct Header {
        uint32_t    magic       = 0x54504D50; // "TPMP" - Transport Protocol Message Protocol
        uint16_t    version     = 1;
        MessageType type;
        uint32_t    length;
        uint32_t    sequence_id;
        uint32_t    session_id;
        uint32_t    checksum;
    } __attribute__((packed));

    Message();
    explicit Message(MessageType type);
    ~Message();

    // Setters
    void setType(MessageType type)            { header_.type = type; }
    void setSequenceId(uint32_t seq_id)       { header_.sequence_id = seq_id; }
    void setSessionId(uint32_t session_id)    { header_.session_id = session_id; }
    
    // Getters
    MessageType getType() const               { return header_.type; }
    uint32_t    getSequenceId() const         { return header_.sequence_id; }
    uint32_t    getSessionId() const          { return header_.session_id; }
    uint32_t    getLength() const             { return header_.length; }

    // Data API
    void addString(const std::string& key, const std::string& value);
    void addInt(const std::string& key, int32_t value);
    void addDouble(const std::string& key, double value);
    void addBool(const std::string& key, bool value);
    void addBinary(const std::string& key, const std::vector<uint8_t>& data);

    std::string              getString(const std::string& key) const;
    int32_t                  getInt(const std::string& key) const;
    double                   getDouble(const std::string& key) const;
    bool                     getBool(const std::string& key) const;
    std::vector<uint8_t>     getBinary(const std::string& key) const;

    bool hasKey(const std::string& key) const;

    // Serijalizacija
    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& data);
    
    // Stream (frame sa prefiksom dužine)
    std::vector<uint8_t> serializeStream() const;
    bool deserializeStream(const std::vector<uint8_t>& data);

    // Validacija
    bool isValid() const;
    void calculateChecksum();
    bool verifyChecksum() const;

    // Utility
    void   clear();
    size_t size() const;
    void   print() const;

private:
    Header                          header_{};
    std::map<std::string, std::string> data_;
    
    uint32_t            calculateCRC32(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> encodeData() const;
    bool                 decodeData(const std::vector<uint8_t>& data);
};

// =========================
// MessageFactory 
// =========================
// Napomena: ostaje u istom headeru radi jednostavnosti; može biti i u posebnom .h/.cpp
class MessageFactory {
public:
    // Connection
    static std::unique_ptr<Message> createConnectRequest(const std::string& client_id);
    static std::unique_ptr<Message> createConnectResponse(bool success, const std::string& reason = "");
    
    // Auth
    static std::unique_ptr<Message> createAuthRequest(const std::string& urn, const std::string& pin = "");
    static std::unique_ptr<Message> createAuthResponse(bool success, const std::string& token = "");
    
    // Registracije
    static std::unique_ptr<Message> createRegisterUser(const std::string& urn);
    static std::unique_ptr<Message> createRegisterDevice(const std::string& uri, VehicleType vehicle_type);
    
    // Usluge
    static std::unique_ptr<Message> createReserveSeat(VehicleType vehicle_type, const std::string& route);
    static std::unique_ptr<Message> createPurchaseTicket(TicketType ticket_type, VehicleType vehicle_type,
                                                         const std::string& route, int passengers = 1);
    
    // Grupe
    static std::unique_ptr<Message> createGroupCreate(const std::string& group_name, const std::string& leader_urn);
    static std::unique_ptr<Message> createDeleteUser(const std::string& urn, const std::string& reason);

    // NEW: članstvo u grupi
    static std::unique_ptr<Message> createAddMemberToGroup(const std::string& group_name,
                                                           const std::string& member_urn,
                                                           const std::string& session_id_str = "");
    static std::unique_ptr<Message> createRemoveMemberFromGroup(const std::string& group_name,
                                                                const std::string& member_urn,
                                                                const std::string& session_id_str = "");
    
    // Admin / update poruke (NOVO)
    static std::unique_ptr<Message> createUpdatePrice(VehicleType vehicle_type,
                                                      TicketType ticket_type,
                                                      double price);

    static std::unique_ptr<Message> createUpdateVehicle(const std::string& uri,
                                                        std::optional<bool> active = {},
                                                        std::optional<std::string> route = {},
                                                        std::optional<VehicleType> type = {});

    static std::unique_ptr<Message> createUpdateCapacity(const std::string& uri,
                                                         int capacity,
                                                         int available_seats);

    // Sistem / odgovori
    static std::unique_ptr<Message> createSuccessResponse(const std::string& message = "",
                                                          const std::map<std::string, std::string>& data = {});
    static std::unique_ptr<Message> createErrorResponse(const std::string& error_message, int error_code = -1);
    static std::unique_ptr<Message> createHeartbeat();
    static std::unique_ptr<Message> createDisconnect();
    static std::unique_ptr<Message> createMulticastUpdate(const std::string& update_type,
                                                          const std::map<std::string, std::string>& data);
};

} // namespace transport

