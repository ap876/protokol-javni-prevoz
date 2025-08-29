#include <iostream>
#include <string>
#include <memory>
#include "client/UserInterface.h"
#include "common/Logger.h"

using namespace transport;

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  -s, --server <address>   Server address (default: localhost)\n";
    std::cout << "  -p, --port <port>        Server port (default: 8080)\n";
    std::cout << "  -u, --urn <urn>          User URN for authentication\n";
    std::cout << "  --ca <file>              CA certificate file (default: certs/ca.crt)\n";
    std::cout << "  -a, --discover           Use multicast auto-discovery (server='auto')\n";
    std::cout << "  -l, --log <file>         Log file path (default: logs/user_client.log)\n";
    std::cout << "  -v, --verbose            Enable verbose logging\n";
    std::cout << "  -h, --help               Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string server_address = "localhost";
    int         port           = 8080;
    std::string user_urn;
    std::string ca_file        = "certs/ca.crt";
    std::string log_file       = "logs/user_client.log";
    bool        verbose        = false;
    bool        discover       = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) server_address = argv[++i];
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) port = std::stoi(argv[++i]);
        } else if (arg == "-u" || arg == "--urn") {
            if (i + 1 < argc) user_urn = argv[++i];
        } else if (arg == "--ca") {
            if (i + 1 < argc) ca_file = argv[++i];
        } else if (arg == "-l" || arg == "--log") {
            if (i + 1 < argc) log_file = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-a" || arg == "--discover") {
            discover = true;
        }
    }

    try {
        // Initialize logger
        auto logger = Logger::getLogger("UserClient");
        logger->initialize(log_file, verbose ? Logger::LogLevel::DEBUG : Logger::LogLevel::INFO);
        
        if (discover) {
            server_address = "auto"; // UserInterface::connect() će pokrenuti multicast DISCOVER
        }

        logger->info("Starting User Client...");
        logger->info("Server: " + server_address + ":" + std::to_string(port));
        logger->info("CA: " + ca_file);

        // Create user interface
        auto ui = std::make_unique<UserInterface>();
        if (verbose) ui->setLogLevel(Logger::LogLevel::DEBUG);

        // Connect to server (ako je "auto", radi multicast DISCOVER/ANNOUNCE)
        logger->info("Connecting to server...");
        if (!ui->connect(server_address, port, ca_file)) {
            logger->error("Failed to connect to server");
            std::cerr << "Failed to connect to server at " << server_address << ":" << port << std::endl;
            return 1;
        }

        logger->info("Connected to server successfully");
        std::cout << "Connected to Central Server at " << server_address << ":" << port << std::endl;

        // Authenticate if URN provided
        if (!user_urn.empty()) {
            std::cout << "Authenticating with URN: " << user_urn << std::endl;
            if (ui->authenticate(user_urn)) {
                std::cout << "Authentication successful!" << std::endl;
            } else {
                std::cout << "Authentication failed!" << std::endl;
            }
        }

        // Start interactive session
        std::cout << "\n=== Public Transport System Client ===\n";
        std::cout << "Type 'help' for available commands or 'quit' to exit\n";
        ui->startInteractiveSession();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

