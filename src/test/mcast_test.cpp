#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <thread>     
#include <chrono>     

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>

int main(int argc, char* argv[]) {
    using boost::asio::ip::udp;

    // default multicast params (isti kao u central_server_main.cpp)
    std::string maddr = "239.192.0.1";
    unsigned short mport = 30001;

    // optional CLI: --maddr ... --mport ...
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--maddr") maddr = argv[++i];
        else if (a == "--mport") mport = static_cast<unsigned short>(std::stoi(argv[++i]));
    }

    try {
        boost::asio::io_context io;
        udp::endpoint listen_ep(udp::v4(), 0); // ephemeral port for client
        udp::socket sock(io);
        sock.open(udp::v4());
        sock.set_option(boost::asio::socket_base::reuse_address(true));
        sock.bind(listen_ep);

        // multicast target
        auto group_addr = boost::asio::ip::make_address(maddr);
        udp::endpoint group_ep(group_addr, mport);

        // Pošalji DISCOVER
        const std::string msg = "DISCOVER";
        sock.send_to(boost::asio::buffer(msg), group_ep);

        // Pokušaj nekoliko puta primiti ANNOUNCE
        std::array<char,512> buf{};
        udp::endpoint sender;
        bool got = false;

        for (int attempt = 0; attempt < 30; ++attempt) {
            boost::system::error_code ec;
            sock.non_blocking(true, ec);

            size_t n = sock.receive_from(boost::asio::buffer(buf), sender, 0, ec);
            if (!ec && n > 0) {
                std::string s(buf.data(), buf.data() + n);
                // ukloni whitespace s kraja
                while (!s.empty() && (s.back()=='\n' || s.back()=='\r' || s.back()==' '))
                    s.pop_back();

                std::cout << "RX from " << sender.address().to_string() << ":" << sender.port()
                          << " -> \"" << s << "\"\n";
                if (s.rfind("ANNOUNCE central ", 0) == 0) {
                    got = true;
                    break;
                }
            }

            // kratka pauza prije novog pokušaja
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (got) {
            std::cout << "[OK] primljen ANNOUNCE od central servera\n";
            return 0;
        } else {
            std::cout << "[FAIL] nisam dobila ANNOUNCE (provjeri da li je central_server pokrenut sa --mcast on)\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "mcast_test error: " << e.what() << "\n";
        return 2;
    }
}

