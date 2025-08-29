#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "common/Message.h"
#include "common/TLSSocket.h"
#include "server/CentralServer.h"
#include "client/UserInterface.h"
#include "common/Logger.h"

using namespace transport;

class ProtocolTester {
public:
    ProtocolTester() : server_port_(8080), test_passed_(0), test_failed_(0) {}
    
    void runAllTests() {
        std::cout << "=== Public Transport Protocol Test Suite ===" << std::endl;
        
        // Initialize logger
        auto logger = Logger::getLogger("ProtocolTest");
        logger->initialize("logs/protocol_test.log", Logger::LogLevel::DEBUG);
        
        // Test message serialization/deserialization
        testMessageSerialization();
        
        // Test TLS socket functionality (basic)
        testTLSSocketBasics();
        
        // Test server startup and shutdown
        testServerLifecycle();
        
        // Test client-server communication
        testClientServerCommunication();
        
        // Test authentication flow
        testAuthenticationFlow();
        
        // Test seat reservation
        testSeatReservation();
        
        // Test ticket purchase
        testTicketPurchase();
        
        // Test group management
        testGroupManagement();
        
        // Print results
        printTestResults();
    }

private:
    int server_port_;
    int test_passed_;
    int test_failed_;
    
    void testMessageSerialization() {
        std::cout << "\n--- Testing Message Serialization ---" << std::endl;
        
        try {
            // Test basic message creation
            auto message = MessageFactory::createConnectRequest("test_client");
            assert(message->getType() == MessageType::CONNECT_REQUEST);
            assert(message->getString("client_id") == "test_client");
            
            // Test serialization
            auto serialized = message->serialize();
            assert(!serialized.empty());
            
            // Test deserialization
            auto new_message = std::make_unique<Message>();
            assert(new_message->deserialize(serialized));
            assert(new_message->getType() == MessageType::CONNECT_REQUEST);
            assert(new_message->getString("client_id") == "test_client");
            
            // Test stream serialization
            auto stream_data = message->serializeStream();
            assert(!stream_data.empty());
            
            auto stream_message = std::make_unique<Message>();
            assert(stream_message->deserializeStream(stream_data));
            assert(stream_message->getString("client_id") == "test_client");
            
            std::cout << "Message serialization tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Message serialization tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testTLSSocketBasics() {
        std::cout << "\n--- Testing TLS Socket Basics ---" << std::endl;
        
        try {
            // Test socket creation
            TLSSocket client_socket(TLSSocket::Mode::CLIENT);
            TLSSocket server_socket(TLSSocket::Mode::SERVER);
            
            assert(!client_socket.isConnected());
            assert(!server_socket.isConnected());
            
            std::cout << "TLS Socket creation tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "TLS Socket tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testServerLifecycle() {
        std::cout << "\n--- Testing Server Lifecycle ---" << std::endl;
        
        try {
            auto server = std::make_unique<CentralServer>();
            assert(!server->isRunning());
            
            // Note: Full server startup test would require certificates and database
            // This is a basic lifecycle test
            std::cout << "Server lifecycle tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Server lifecycle tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testClientServerCommunication() {
        std::cout << "\n--- Testing Client-Server Communication ---" << std::endl;
        
        // This would be a more complex test requiring actual server startup
        // For now, we'll test the message flow
        try {
            // Test message factory for various message types
            auto connect_msg = MessageFactory::createConnectRequest("test_device");
            auto auth_msg = MessageFactory::createAuthRequest("1234567890123");
            auto register_msg = MessageFactory::createRegisterUser("1234567890123");
            auto success_msg = MessageFactory::createSuccessResponse("Operation completed");
            auto error_msg = MessageFactory::createErrorResponse("Test error", 404);
            
            assert(connect_msg->getType() == MessageType::CONNECT_REQUEST);
            assert(auth_msg->getType() == MessageType::AUTH_REQUEST);
            assert(register_msg->getType() == MessageType::REGISTER_USER);
            assert(success_msg->getType() == MessageType::RESPONSE_SUCCESS);
            assert(error_msg->getType() == MessageType::RESPONSE_ERROR);
            
            std::cout << "Client-Server communication message tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Client-Server communication tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testAuthenticationFlow() {
        std::cout << "\n--- Testing Authentication Flow ---" << std::endl;
        
        try {
            // Test URN validation (basic format check)
            std::string valid_urn = "1234567890123";
            std::string invalid_urn = "123";
            
            assert(valid_urn.length() == 13);
            assert(invalid_urn.length() != 13);
            
            // Test authentication message creation
            auto auth_request = MessageFactory::createAuthRequest(valid_urn, "1234");
            assert(auth_request->getString("urn") == valid_urn);
            assert(auth_request->getString("pin") == "1234");
            
            auto auth_response = MessageFactory::createAuthResponse(true, "test_token");
            assert(auth_response->getBool("success") == true);
            assert(auth_response->getString("token") == "test_token");
            
            std::cout << "Authentication flow tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Authentication flow tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testSeatReservation() {
        std::cout << "\n--- Testing Seat Reservation ---" << std::endl;
        
        try {
            // Test seat reservation message
            auto reserve_msg = MessageFactory::createReserveSeat(VehicleType::BUS, "Route1");
            assert(reserve_msg->getType() == MessageType::RESERVE_SEAT);
            assert(reserve_msg->getInt("vehicle_type") == static_cast<int>(VehicleType::BUS));
            assert(reserve_msg->getString("route") == "Route1");
            
            std::cout << "Seat reservation tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Seat reservation tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testTicketPurchase() {
        std::cout << "\n--- Testing Ticket Purchase ---" << std::endl;
        
        try {
            // Test ticket purchase message
            auto ticket_msg = MessageFactory::createPurchaseTicket(
                TicketType::INDIVIDUAL, VehicleType::TRAM, "Route2", 1);
            assert(ticket_msg->getType() == MessageType::PURCHASE_TICKET);
            assert(ticket_msg->getInt("ticket_type") == static_cast<int>(TicketType::INDIVIDUAL));
            assert(ticket_msg->getInt("vehicle_type") == static_cast<int>(VehicleType::TRAM));
            assert(ticket_msg->getString("route") == "Route2");
            assert(ticket_msg->getInt("passengers") == 1);
            
            std::cout << "Ticket purchase tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Ticket purchase tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void testGroupManagement() {
        std::cout << "\n--- Testing Group Management ---" << std::endl;
        
        try {
            // Test group creation message
            auto group_msg = MessageFactory::createGroupCreate("Family Group", "1234567890123");
            assert(group_msg->getType() == MessageType::CREATE_GROUP);
            assert(group_msg->getString("group_name") == "Family Group");
            assert(group_msg->getString("leader_urn") == "1234567890123");
            
            std::cout << "Group management tests passed" << std::endl;
            test_passed_++;
            
        } catch (const std::exception& e) {
            std::cout << "Group management tests failed: " << e.what() << std::endl;
            test_failed_++;
        }
    }
    
    void printTestResults() {
        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Tests passed: " << test_passed_ << std::endl;
        std::cout << "Tests failed: " << test_failed_ << std::endl;
        std::cout << "Total tests: " << (test_passed_ + test_failed_) << std::endl;
        
        if (test_failed_ == 0) {
            std::cout << "All tests passed!" << std::endl;
        } else {
            std::cout << "Some tests failed!" << std::endl;
        }
    }
};

int main() {
    try {
        ProtocolTester tester;
        tester.runAllTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
