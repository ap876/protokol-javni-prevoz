#include <iostream>
#include <string>
#include <csignal>
#include <memory>
#include <thread>
#include <chrono>
#include "server/CentralServer.h"
#include "common/Logger.h"

using namespace transport;

std::unique_ptr<CentralServer> server;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down server..." << std::endl;
    if (server) {
        server->stop();
    }
    std::exit(0);
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -p, --port <port>        Server port (default: 8080)\n"
              << "  -c, --config <file>      Configuration file\n"
              << "  -d, --database <path>    Database file path\n"
              << "  --cert <file>            TLS certificate file\n"
              << "  --key <file>             TLS private key file\n"
              << "  -l, --log <file>         Log file path\n"
              << "  -v, --verbose            Enable verbose logging\n"
              << "  --mcast on|off           Enable UDP multicast DISCOVER/ANNOUNCE (default: off)\n"
              << "  --maddr <ip>             Multicast address (default: 239.192.0.1)\n"
              << "  --mport <port>           Multicast port    (default: 30001)\n"
              << "  -h, --help               Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Default configuration
    int port = 8080;
    std::string config_file;
    std::string database_path = "central_server.db";
    std::string cert_file = "certs/server.crt";
    std::string key_file  = "certs/server.key";
    std::string log_file  = "logs/central_server.log";
    bool verbose = false;

    // Multicast defaults (moraju odgovarati CentralServer defaultima)
    bool        mcast_on  = false;
    std::string mcast_addr = "239.192.0.1";
    int         mcast_port = 30001;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) port = std::stoi(argv[++i]);
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) config_file = argv[++i];
        } else if (arg == "-d" || arg == "--database") {
            if (i + 1 < argc) database_path = argv[++i];
        } else if (arg == "--cert") {
            if (i + 1 < argc) cert_file = argv[++i];
        } else if (arg == "--key") {
            if (i + 1 < argc) key_file = argv[++i];
        } else if (arg == "-l" || arg == "--log") {
            if (i + 1 < argc) log_file = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--mcast") {
            if (i + 1 < argc) {
                std::string v = argv[++i];
                if (v == "on" || v == "ON" || v == "1" || v == "true") mcast_on = true;
                else if (v == "off" || v == "OFF" || v == "0" || v == "false") mcast_on = false;
            }
        } else if (arg == "--maddr") {
            if (i + 1 < argc) mcast_addr = argv[++i];
        } else if (arg == "--mport") {
            if (i + 1 < argc) mcast_port = std::stoi(argv[++i]);
        }
    }

    // Set up signal handling
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // Initialize logger
        auto logger = Logger::getLogger("CentralServer");
        logger->initialize(log_file, verbose ? Logger::LogLevel::DEBUG : Logger::LogLevel::INFO);
        
        logger->info("Starting Central Server...");
        logger->info("Port: " + std::to_string(port));
        logger->info("Database: " + database_path);
        logger->info("Certificate: " + cert_file);
        logger->info("Key: " + key_file);
        logger->info(std::string("Multicast: ") + (mcast_on ? "ON" : "OFF")
                     + " addr=" + mcast_addr + " port=" + std::to_string(mcast_port));

        // Create and configure server
        server = std::make_unique<CentralServer>();
        server->setDatabasePath(database_path);
        server->setCertificatePath(cert_file, key_file);

        // >>> NOVO: uključi multicast po CLI-u
        server->setMulticastEnabled(mcast_on);
        server->setMulticastAddress(mcast_addr);
        server->setMulticastPort(mcast_port);
        
        if (verbose) {
            server->setLogLevel(Logger::LogLevel::DEBUG);
        }

        // Load configuration if provided (npr. kasnije može nadjačati gornje settere)
        if (!config_file.empty()) {
            logger->info("Loading configuration from: " + config_file);
            if (!server->loadConfiguration(config_file)) {
                logger->error("Failed to load configuration file: " + config_file);
                return 1;
            }
        }

        // Start server
        logger->info("Starting server on port " + std::to_string(port) + "...");
        if (!server->start(port, config_file)) {
            logger->error("Failed to start server");
            return 1;
        }

        logger->info("Central Server started successfully");
        std::cout << "Central Server is running on port " << port << std::endl;
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

