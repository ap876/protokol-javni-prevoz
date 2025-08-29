#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include "client/PaymentDevice.h"
#include "common/Logger.h"

using namespace transport;

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --uri <uri>              Device URI identifier" << std::endl;
    std::cout << "  --vehicle-type <type>    Vehicle type (bus/tram/trolleybus)" << std::endl;
    std::cout << "  -s, --server <address>   Vehicle server address (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>        Vehicle server port (default: 8081)" << std::endl;
    std::cout << "  --ca <file>              CA certificate file" << std::endl;
    std::cout << "  -l, --log <file>         Log file path" << std::endl;
    std::cout << "  -v, --verbose            Enable verbose logging" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string device_uri = "DEVICE_001";
    std::string vehicle_type = "bus";
    std::string server_address = "localhost";
    int port = 8081;
    std::string ca_file = "certs/ca.crt";
    std::string log_file = "logs/payment_device.log";
    bool verbose = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--uri") {
            if (i + 1 < argc) {
                device_uri = argv[++i];
            }
        }
        else if (arg == "--vehicle-type") {
            if (i + 1 < argc) {
                vehicle_type = argv[++i];
            }
        }
        else if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) {
                server_address = argv[++i];
            }
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        }
        else if (arg == "--ca") {
            if (i + 1 < argc) {
                ca_file = argv[++i];
            }
        }
        else if (arg == "-l" || arg == "--log") {
            if (i + 1 < argc) {
                log_file = argv[++i];
            }
        }
        else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        }
    }

    try {
        // Initialize logger
        auto logger = Logger::getLogger("PaymentDevice");
        logger->initialize(log_file, verbose ? Logger::LogLevel::DEBUG : Logger::LogLevel::INFO);
        
        logger->info("Starting Payment Device...");
        logger->info("Device URI: " + device_uri);
        logger->info("Vehicle Type: " + vehicle_type);
        logger->info("Server: " + server_address + ":" + std::to_string(port));

        // Create payment device
        auto device = std::make_unique<PaymentDevice>();

        // Connect to vehicle server
        logger->info("Connecting to vehicle server...");
        if (!device->connect(server_address, port)) {
            logger->error("Failed to connect to vehicle server");
            std::cerr << "Failed to connect to vehicle server at " << server_address << ":" << port << std::endl;
            return 1;
        }

        logger->info("Connected to vehicle server successfully");
        std::cout << "Payment Device (" << device_uri << ") connected to " << vehicle_type << " server" << std::endl;
        std::cout << "Server: " << server_address << ":" << port << std::endl;
        std::cout << "Device ready for transactions..." << std::endl;

        // Simulate payment device operations
        std::cout << "\n=== Payment Device Simulator ===" << std::endl;
        std::cout << "Device URI: " << device_uri << std::endl;
        std::cout << "Vehicle Type: " << vehicle_type << std::endl;
        std::cout << "Status: Ready for card reading and payments" << std::endl;
        std::cout << "Press Ctrl+C to stop the device" << std::endl;

        // Keep the device running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // In a real implementation, this would handle card reading,
            // user interactions, and payment processing
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
