#include "common/Message.h"

#include <sstream>
#include <iostream>
#include <cstring>
#include <ctime>
#include <arpa/inet.h>

namespace transport {

// =========================
// Message implementacija
// =========================

Message::Message() {
    clear();
}

Message::Message(MessageType type) {
    clear();
    header_.type = type;
}

Message::~Message() = default;

void Message::addString(const std::string& key, const std::string& value) {
    data_[key] = value;
    header_.length = static_cast<uint32_t>(encodeData().size());
}

void Message::addInt(const std::string& key, int32_t value) {
    data_[key] = std::to_string(value);
    header_.length = static_cast<uint32_t>(encodeData().size());
}

void Message::addDouble(const std::string& key, double value) {
    data_[key] = std::to_string(value);
    header_.length = static_cast<uint32_t>(encodeData().size());
}

void Message::addBool(const std::string& key, bool value) {
    data_[key] = value ? "true" : "false";
    header_.length = static_cast<uint32_t>(encodeData().size());
}

void Message::addBinary(const std::string& key, const std::vector<uint8_t>& binary_data) {
    std::string encoded;
    encoded.reserve(binary_data.size() * 3);
    for (uint8_t byte : binary_data) {
        encoded += std::to_string(byte);
        encoded += ",";
    }
    if (!encoded.empty()) encoded.pop_back();
    data_[key] = std::move(encoded);
    header_.length = static_cast<uint32_t>(encodeData().size());
}

std::string Message::getString(const std::string& key) const {
    auto it = data_.find(key);
    return it != data_.end() ? it->second : "";
}

int32_t Message::getInt(const std::string& key) const {
    auto it = data_.find(key);
    return it != data_.end() ? std::stoi(it->second) : 0;
}

double Message::getDouble(const std::string& key) const {
    auto it = data_.find(key);
    return it != data_.end() ? std::stod(it->second) : 0.0;
}

bool Message::getBool(const std::string& key) const {
    auto it = data_.find(key);
    return it != data_.end() && it->second == "true";
}

std::vector<uint8_t> Message::getBinary(const std::string& key) const {
    std::vector<uint8_t> result;
    auto it = data_.find(key);
    if (it != data_.end()) {
        std::stringstream ss(it->second);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty())
                result.push_back(static_cast<uint8_t>(std::stoi(token)));
        }
    }
    return result;
}

bool Message::hasKey(const std::string& key) const {
    return data_.find(key) != data_.end();
}

std::vector<uint8_t> Message::serialize() const {
    std::vector<uint8_t> result;
    
    // Header → mrežni redoslijed
    Header net_header = header_;
    net_header.magic       = htonl(net_header.magic);
    net_header.version     = htons(net_header.version);
    net_header.type        = static_cast<MessageType>(htons(static_cast<uint16_t>(net_header.type)));
    net_header.length      = htonl(net_header.length);
    net_header.sequence_id = htonl(net_header.sequence_id);
    net_header.session_id  = htonl(net_header.session_id);
    net_header.checksum    = htonl(net_header.checksum);
    
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&net_header);
    result.insert(result.end(), header_bytes, header_bytes + sizeof(Header));
    
    // Podaci
    std::vector<uint8_t> data_bytes = encodeData();
    result.insert(result.end(), data_bytes.begin(), data_bytes.end());
    
    return result;
}

bool Message::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(Header)) return false;
    
    // Header ← host redoslijed
    std::memcpy(&header_, data.data(), sizeof(Header));
    header_.magic       = ntohl(header_.magic);
    header_.version     = ntohs(header_.version);
    header_.type        = static_cast<MessageType>(ntohs(static_cast<uint16_t>(header_.type)));
    header_.length      = ntohl(header_.length);
    header_.sequence_id = ntohl(header_.sequence_id);
    header_.session_id  = ntohl(header_.session_id);
    header_.checksum    = ntohl(header_.checksum);
    
    if (header_.magic != 0x54504D50) return false;
    
    if (data.size() >= sizeof(Header) + header_.length) {
        std::vector<uint8_t> data_bytes(data.begin() + sizeof(Header),
                                        data.begin() + sizeof(Header) + header_.length);
        return decodeData(data_bytes);
    }
    return false;
}

std::vector<uint8_t> Message::serializeStream() const {
    std::vector<uint8_t> serialized = serialize();
    std::vector<uint8_t> result;
    
    uint32_t total_length = htonl(static_cast<uint32_t>(serialized.size()));
    const uint8_t* length_bytes = reinterpret_cast<const uint8_t*>(&total_length);
    result.insert(result.end(), length_bytes, length_bytes + sizeof(uint32_t));
    result.insert(result.end(), serialized.begin(), serialized.end());
    
    return result;
}

bool Message::deserializeStream(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(uint32_t)) return false;
    
    uint32_t length = 0;
    std::memcpy(&length, data.data(), sizeof(uint32_t));
    length = ntohl(length);
    
    if (data.size() < sizeof(uint32_t) + length) return false;
    
    std::vector<uint8_t> message_data(data.begin() + sizeof(uint32_t),
                                      data.begin() + sizeof(uint32_t) + length);
    return deserialize(message_data);
}

void Message::calculateChecksum() {
    // checksum se računa nad cijelim frameom sa checksum=0
    header_.checksum = 0;
    std::vector<uint8_t> bytes = serialize();
    header_.checksum = calculateCRC32(bytes);
}

bool Message::verifyChecksum() const {
    // privremeno poništi checksum pri računanju
    uint32_t original_checksum = header_.checksum;
    const_cast<Message*>(this)->header_.checksum = 0;
    std::vector<uint8_t> bytes = serialize();
    uint32_t computed = calculateCRC32(bytes);
    const_cast<Message*>(this)->header_.checksum = original_checksum;
    return original_checksum == computed;
}

bool Message::isValid() const {
    return header_.magic == 0x54504D50 &&
           header_.version == 1 &&
           verifyChecksum();
}

void Message::clear() {
    header_ = {};
    header_.magic = 0x54504D50;
    header_.version = 1;
    data_.clear();
}

size_t Message::size() const {
    return sizeof(Header) + header_.length;
}

void Message::print() const {
    std::cout << "Message Type: " << static_cast<int>(header_.type) << "\n"
              << "Sequence ID: " << header_.sequence_id << "\n"
              << "Session ID : " << header_.session_id << "\n"
              << "Length     : " << header_.length << "\n"
              << "Data:\n";
    for (const auto& pair : data_) {
        std::cout << "  " << pair.first << ": " << pair.second << "\n";
    }
}

uint32_t Message::calculateCRC32(const std::vector<uint8_t>& data) const {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(static_cast<int32_t>(crc) & 1)));
        }
    }
    return ~crc;
}

std::vector<uint8_t> Message::encodeData() const {
    std::vector<uint8_t> result;
    result.reserve(data_.size() * 12); // gruba procjena
    for (const auto& pair : data_) {
        uint32_t key_len = htonl(static_cast<uint32_t>(pair.first.length()));
        uint32_t val_len = htonl(static_cast<uint32_t>(pair.second.length()));
        
        const uint8_t* key_len_bytes = reinterpret_cast<const uint8_t*>(&key_len);
        const uint8_t* val_len_bytes = reinterpret_cast<const uint8_t*>(&val_len);
        
        result.insert(result.end(), key_len_bytes, key_len_bytes + sizeof(uint32_t));
        result.insert(result.end(), pair.first.begin(), pair.first.end());
        result.insert(result.end(), val_len_bytes, val_len_bytes + sizeof(uint32_t));
        result.insert(result.end(), pair.second.begin(), pair.second.end());
    }
    return result;
}

bool Message::decodeData(const std::vector<uint8_t>& data) {
    data_.clear();
    size_t pos = 0;
    
    while (pos + sizeof(uint32_t) <= data.size()) {
        uint32_t key_len = 0;
        std::memcpy(&key_len, data.data() + pos, sizeof(uint32_t));
        key_len = ntohl(key_len);
        pos += sizeof(uint32_t);
        if (pos + key_len > data.size()) break;
        
        std::string key(data.begin() + pos, data.begin() + pos + key_len);
        pos += key_len;
        if (pos + sizeof(uint32_t) > data.size()) break;
        
        uint32_t val_len = 0;
        std::memcpy(&val_len, data.data() + pos, sizeof(uint32_t));
        val_len = ntohl(val_len);
        pos += sizeof(uint32_t);
        if (pos + val_len > data.size()) break;
        
        std::string value(data.begin() + pos, data.begin() + pos + val_len);
        pos += val_len;
        data_[std::move(key)] = std::move(value);
    }
    return pos == data.size();
}

// =========================
// MessageFactory implementacija
// =========================

std::unique_ptr<Message> MessageFactory::createConnectRequest(const std::string& client_id) {
    auto message = std::make_unique<Message>(MessageType::CONNECT_REQUEST);
    message->addString("client_id", client_id);
    message->addString("protocol_version", "1.0");
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createConnectResponse(bool success, const std::string& reason) {
    auto message = std::make_unique<Message>(MessageType::CONNECT_RESPONSE);
    message->addBool("success", success);
    if (!reason.empty()) message->addString("reason", reason);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createAuthRequest(const std::string& urn, const std::string& pin) {
    auto message = std::make_unique<Message>(MessageType::AUTH_REQUEST);
    message->addString("urn", urn);
    if (!pin.empty()) message->addString("pin", pin);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createAuthResponse(bool success, const std::string& token) {
    auto message = std::make_unique<Message>(MessageType::AUTH_RESPONSE);
    message->addBool("success", success);
    if (!token.empty()) {
        // klijentu vraćamo "token" (string); klijent ga šalje nazad kao "session_id" u narednim porukama
        message->addString("token", token);
    }
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createRegisterUser(const std::string& urn) {
    auto message = std::make_unique<Message>(MessageType::REGISTER_USER);
    message->addString("urn", urn);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createRegisterDevice(const std::string& uri, VehicleType vehicle_type) {
    auto message = std::make_unique<Message>(MessageType::REGISTER_DEVICE);
    message->addString("uri", uri);
    message->addInt("vehicle_type", static_cast<int>(vehicle_type));
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createReserveSeat(VehicleType vehicle_type, const std::string& route) {
    auto message = std::make_unique<Message>(MessageType::RESERVE_SEAT);
    message->addInt("vehicle_type", static_cast<int>(vehicle_type));
    if (!route.empty()) message->addString("route", route);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createPurchaseTicket(TicketType ticket_type, VehicleType vehicle_type,
                                                             const std::string& route, int passengers) {
    auto message = std::make_unique<Message>(MessageType::PURCHASE_TICKET);
    message->addInt("ticket_type", static_cast<int>(ticket_type));
    message->addInt("vehicle_type", static_cast<int>(vehicle_type));
    if (!route.empty()) message->addString("route", route);
    message->addInt("passengers", passengers);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createGroupCreate(const std::string& group_name, const std::string& leader_urn) {
    auto message = std::make_unique<Message>(MessageType::CREATE_GROUP);
    message->addString("group_name", group_name);
    if (!leader_urn.empty()) message->addString("leader_urn", leader_urn);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createDeleteUser(const std::string& urn, const std::string& reason) {
    auto message = std::make_unique<Message>(MessageType::DELETE_USER);
    message->addString("urn", urn);
    if (!reason.empty()) message->addString("reason", reason);
    message->calculateChecksum();
    return message;
}

// NEW: članstvo u grupi
std::unique_ptr<Message> MessageFactory::createAddMemberToGroup(const std::string& group_name,
                                                                const std::string& member_urn,
                                                                const std::string& session_id_str) {
    auto message = std::make_unique<Message>(MessageType::ADD_MEMBER_TO_GROUP);
    message->addString("group_name", group_name);
    message->addString("urn", member_urn);
    if (!session_id_str.empty()) message->addString("session_id", session_id_str);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createRemoveMemberFromGroup(const std::string& group_name,
                                                                     const std::string& member_urn,
                                                                     const std::string& session_id_str) {
    auto message = std::make_unique<Message>(MessageType::DELETE_GROUP_MEMBER); // koristi postojeći tip
    message->addString("group_name", group_name);
    message->addString("urn", member_urn);
    if (!session_id_str.empty()) message->addString("session_id", session_id_str);
    message->calculateChecksum();
    return message;
}

// admin ažuriranja

std::unique_ptr<Message> MessageFactory::createUpdatePrice(VehicleType vehicle_type,
                                                           TicketType ticket_type,
                                                           double price) {
    auto message = std::make_unique<Message>(MessageType::UPDATE_PRICE);
    message->addInt("vehicle_type", static_cast<int>(vehicle_type));
    message->addInt("ticket_type", static_cast<int>(ticket_type));
    message->addString("price", std::to_string(price));
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createUpdateVehicle(const std::string& uri,
                                                             std::optional<bool> active,
                                                             std::optional<std::string> route,
                                                             std::optional<VehicleType> type) {
    auto message = std::make_unique<Message>(MessageType::UPDATE_VEHICLE);
    message->addString("uri", uri);
    if (active.has_value()) message->addInt("active", *active ? 1 : 0);
    if (route.has_value())  message->addString("route", *route);
    if (type.has_value())   message->addInt("vehicle_type", static_cast<int>(*type));
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createUpdateCapacity(const std::string& uri,
                                                              int capacity,
                                                              int available_seats) {
    auto message = std::make_unique<Message>(MessageType::UPDATE_CAPACITY);
    message->addString("uri", uri);
    message->addInt("capacity", capacity);
    message->addInt("available_seats", available_seats);
    message->calculateChecksum();
    return message;
}

// Sistem / servisne
std::unique_ptr<Message> MessageFactory::createSuccessResponse(const std::string& message_text,
                                                              const std::map<std::string, std::string>& data) {
    auto response = std::make_unique<Message>(MessageType::RESPONSE_SUCCESS);
    if (!message_text.empty()) response->addString("message", message_text);
    for (const auto& pair : data) response->addString(pair.first, pair.second);
    response->calculateChecksum();
    return response;
}

std::unique_ptr<Message> MessageFactory::createErrorResponse(const std::string& error_message, int error_code) {
    auto message = std::make_unique<Message>(MessageType::RESPONSE_ERROR);
    message->addString("error", error_message);
    message->addInt("error_code", error_code);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createHeartbeat() {
    auto message = std::make_unique<Message>(MessageType::HEARTBEAT);
    message->addString("timestamp", std::to_string(std::time(nullptr)));
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createDisconnect() {
    auto message = std::make_unique<Message>(MessageType::DISCONNECT);
    message->calculateChecksum();
    return message;
}

std::unique_ptr<Message> MessageFactory::createMulticastUpdate(const std::string& update_type,
                                                               const std::map<std::string, std::string>& data) {
    auto message = std::make_unique<Message>(MessageType::MULTICAST_UPDATE);
    message->addString("update_type", update_type);
    for (const auto& pair : data) message->addString(pair.first, pair.second);
    message->calculateChecksum();
    return message;
}

} // namespace transport

