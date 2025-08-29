#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include "common/TLSSocket.h"
#include "common/Message.h"
#include "common/Logger.h"

using namespace transport;

class BenchmarkTester {
public:
    BenchmarkTester() : total_messages_(0), successful_messages_(0), failed_messages_(0) {}
    
    void runBenchmark(const std::string& server, int port, int connections, int duration_seconds) {
        std::cout << "=== Transport Protocol Benchmark Test ===" << std::endl;
        std::cout << "Server: " << server << ":" << port << std::endl;
        std::cout << "Concurrent connections: " << connections << std::endl;
        std::cout << "Duration: " << duration_seconds << " seconds" << std::endl;
        
        auto logger = Logger::getLogger("BenchmarkTest");
        logger->initialize("logs/benchmark_test.log", Logger::LogLevel::INFO);
        
        std::vector<std::thread> threads;
        std::atomic<bool> stop_flag(false);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Start benchmark threads
        for (int i = 0; i < connections; ++i) {
            threads.emplace_back([this, &server, port, &stop_flag, i]() {
                runClientBenchmark(server, port, stop_flag, i);
            });
        }
        
        // Run for specified duration
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
        stop_flag = true;
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Print results
        printResults(elapsed.count());
    }

private:
    std::atomic<int> total_messages_;
    std::atomic<int> successful_messages_;
    std::atomic<int> failed_messages_;
    
    void runClientBenchmark(const std::string& server, int port, std::atomic<bool>& stop_flag, int client_id) {
        try {
            TLSSocket socket(TLSSocket::Mode::CLIENT);
            
            if (!socket.connect(server, port)) {
                std::cerr << "Client " << client_id << " failed to connect" << std::endl;
                return;
            }
            
            // Send connect message
            auto connect_msg = MessageFactory::createConnectRequest("benchmark_client_" + std::to_string(client_id));
            if (!socket.sendMessage(*connect_msg)) {
                return;
            }
            
            auto response = socket.receiveMessage();
            if (!response || response->getType() != MessageType::CONNECT_RESPONSE) {
                return;
            }
            
            // Benchmark loop
            while (!stop_flag) {
                // Send various message types
                if (sendBenchmarkMessages(socket, client_id)) {
                    successful_messages_++;
                } else {
                    failed_messages_++;
                }
                total_messages_++;
                
                // Small delay to prevent overwhelming the server
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Client " << client_id << " exception: " << e.what() << std::endl;
        }
    }
    
    bool sendBenchmarkMessages(TLSSocket& socket, int client_id) {
        try {
            // Test different message types
            std::vector<std::unique_ptr<Message>> test_messages;
            
            // User registration
            test_messages.push_back(MessageFactory::createRegisterUser("1234567890" + std::to_string(client_id % 1000)));
            
            // Authentication
            test_messages.push_back(MessageFactory::createAuthRequest("1234567890" + std::to_string(client_id % 1000)));
            
            // Seat reservation
            test_messages.push_back(MessageFactory::createReserveSeat(VehicleType::BUS, "Route" + std::to_string(client_id % 10)));
            
            // Ticket purchase
            test_messages.push_back(MessageFactory::createPurchaseTicket(TicketType::INDIVIDUAL, VehicleType::BUS, "Route" + std::to_string(client_id % 10)));
            
            for (auto& message : test_messages) {
                if (!socket.sendMessage(*message)) {
                    return false;
                }
                
                auto response = socket.receiveMessage();
                if (!response) {
                    return false;
                }
            }
            
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    void printResults(long long elapsed_ms) {
        std::cout << "\n=== Benchmark Results ===" << std::endl;
        std::cout << "Total messages sent: " << total_messages_.load() << std::endl;
        std::cout << "Successful messages: " << successful_messages_.load() << std::endl;
        std::cout << "Failed messages: " << failed_messages_.load() << std::endl;
        std::cout << "Success rate: " << (100.0 * successful_messages_.load() / total_messages_.load()) << "%" << std::endl;
        std::cout << "Elapsed time: " << elapsed_ms << " ms" << std::endl;
        std::cout << "Messages per second: " << (1000.0 * total_messages_.load() / elapsed_ms) << std::endl;
        std::cout << "Average response time: " << (elapsed_ms / static_cast<double>(total_messages_.load())) << " ms" << std::endl;
    }
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --server <address>       Server address (default: localhost)" << std::endl;
    std::cout << "  --port <port>            Server port (default: 8080)" << std::endl;
    std::cout << "  --connections <num>      Number of concurrent connections (default: 10)" << std::endl;
    std::cout << "  --duration <seconds>     Test duration in seconds (default: 60)" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string server = "localhost";
    int port = 8080;
    int connections = 10;
    int duration = 60;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--server") {
            if (i + 1 < argc) {
                server = argv[++i];
            }
        }
        else if (arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        }
        else if (arg == "--connections") {
            if (i + 1 < argc) {
                connections = std::stoi(argv[++i]);
            }
        }
        else if (arg == "--duration") {
            if (i + 1 < argc) {
                duration = std::stoi(argv[++i]);
            }
        }
    }

    try {
        BenchmarkTester tester;
        tester.runBenchmark(server, port, connections, duration);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
