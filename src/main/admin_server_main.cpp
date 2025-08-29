#include <iostream>
#include <string>
#include <csignal>
#include <memory>
#include <thread>
#include <chrono>
#include "server/ServerBase.h"
#include "server/AdminServer.h"
#include "common/Logger.h"

using namespace transport;

std::unique_ptr<ServerBase> server;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down server..." << std::endl;
    if (server) {
        server->stop();
    }
    exit(0);
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port <port>        Server port (default: 8090)" << std::endl;
    std::cout << "  -c, --config <file>      Configuration file" << std::endl;
    std::cout << "  --central-server <addr>  Central server address (default: localhost:8080)" << std::endl;
    std::cout << "  --cert <file>            TLS certificate file" << std::endl;
    std::cout << "  --key <file>             TLS private key file" << std::endl;
    std::cout << "  -l, --log <file>         Log file path" << std::endl;
    std::cout << "  -v, --verbose            Enable verbose logging" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    int port = 8090;
    std::string config_file;
    std::string central_server = "localhost:8080";
    std::string cert_file = "certs/server.crt";
    std::string key_file = "certs/server.key";
    std::string log_file = "logs/admin_server.log";
    bool verbose = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            }
        }
        else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_file = argv[++i];
            }
        }
        else if (arg == "--central-server") {
            if (i + 1 < argc) {
                central_server = argv[++i];
            }
        }
        else if (arg == "--cert") {
            if (i + 1 < argc) {
                cert_file = argv[++i];
            }
        }
        else if (arg == "--key") {
            if (i + 1 < argc) {
                key_file = argv[++i];
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

    // Set up signal handling
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        // Initialize logger
        auto logger = Logger::getLogger("AdminServer");
        logger->initialize(log_file, verbose ? Logger::LogLevel::DEBUG : Logger::LogLevel::INFO);
        
        logger->info("Starting Admin Server...");
        logger->info("Port: " + std::to_string(port));
        logger->info("Central Server: " + central_server);
        logger->info("Certificate: " + cert_file);
        logger->info("Key: " + key_file);

        // Create and configure server
        server = std::make_unique<AdminServer>();
        server->setCertificates(cert_file, key_file);
        
        if (verbose) {
            server->setLogLevel(Logger::LogLevel::DEBUG);
        }

        // Load configuration if provided
        if (!config_file.empty()) {
            logger->info("Loading configuration from: " + config_file);
            if (!server->loadConfiguration(config_file)) {
                logger->error("Failed to load configuration file: " + config_file);
                return 1;
            }
        }

        // Start server
        logger->info("Starting admin server on port " + std::to_string(port) + "...");
        if (!server->start(port, config_file)) {
            logger->error("Failed to start server");
            return 1;
        }

        logger->info("Admin Server started successfully");
        std::cout << "Admin Server is running on port " << port << std::endl;
        std::cout << "Connected to Central Server: " << central_server << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;

        // Keep the server running
        while (server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
