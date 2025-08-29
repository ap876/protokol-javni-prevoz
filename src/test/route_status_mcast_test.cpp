// src/test/route_status_mcast_test.cpp
#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using boost::asio::ip::udp;

// mali helper za OK/FAIL
static void ok(const char* what, bool cond) {
    std::cout << (cond ? "[OK] " : "[FAIL] ") << what << std::endl;
    if (!cond) std::abort();
}

static void receiver_thread_fn(std::atomic<bool>& got_all,
                               std::vector<std::string>& captured,
                               const std::string& maddr,
                               unsigned short mport,
                               int expected_msgs)
{
    try {
        boost::asio::io_context io;
        udp::socket sock(io);
        udp::endpoint listen_ep(udp::v4(), mport);
        sock.open(udp::v4());
        sock.set_option(boost::asio::socket_base::reuse_address(true));
        sock.bind(listen_ep);

        auto group_addr = boost::asio::ip::make_address(maddr);
        sock.set_option(boost::asio::ip::multicast::join_group(group_addr));
        sock.set_option(boost::asio::ip::multicast::enable_loopback(true));

        std::array<char, 1024> buf{};
        udp::endpoint sender_ep;

        while (!got_all.load()) {
            boost::system::error_code ec;
            size_t n = sock.receive_from(boost::asio::buffer(buf), sender_ep, 0, ec);
            if (ec) continue;
            std::string msg(buf.data(), buf.data() + n);
            captured.push_back(msg);
            if ((int)captured.size() >= expected_msgs) {
                got_all.store(true);
            }
        }

        // leave group (best-effort)
        boost::system::error_code ec2;
        sock.set_option(boost::asio::ip::multicast::leave_group(group_addr), ec2);
        (void)ec2;
    } catch (...) {
        
    }
}

static void sender_send_lines(const std::vector<std::string>& lines,
                              const std::string& maddr,
                              unsigned short mport)
{
    boost::asio::io_context io;
    udp::socket sock(io, udp::v4());
    auto group_addr = boost::asio::ip::make_address(maddr);
    udp::endpoint group_ep(group_addr, mport);

    for (const auto& s : lines) {
        boost::system::error_code ec;
        sock.send_to(boost::asio::buffer(s), group_ep, 0, ec);
        // kratka pauza da receiver stigne pročitati
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() {
    const std::string MADDR = "239.192.0.1";
    const unsigned short MPORT = 30001;

    // poruke nalik "server-side" update-ovima
    std::vector<std::string> msgs = {
        "ROUTE_STATUS route=R1 status=OK seats=12",
        "ROUTE_STATUS route=R7 status=FULL seats=0",
        "ROUTE_STATUS route=R2 status=OK seats=5"
    };

    std::atomic<bool> got_all{false};
    std::vector<std::string> captured;
    captured.reserve(msgs.size());

    // start receiver
    std::thread rx(receiver_thread_fn, std::ref(got_all), std::ref(captured), MADDR, MPORT, (int)msgs.size());

    // daj receiveru mrvu vremena da se bind-uje i join-uje
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // pošalji poruke
    sender_send_lines(msgs, MADDR, MPORT);

    // čekaj do 2s
    auto start = std::chrono::steady_clock::now();
    while (!got_all.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) break;
    }
    got_all.store(true);

    rx.join();

    // provjere
    ok("receiver captured something", !captured.empty());
    ok("receiver captured all", (int)captured.size() >= (int)msgs.size());

    // provjeri da sadrži očekivane linije (po sadržaju)
    auto contains = [&](const std::string& needle){
        for (auto& s : captured) if (s == needle) return true;
        return false;
    };
    for (auto& m : msgs) {
        ok(("captured: " + m).c_str(), contains(m));
    }

    std::cout << "All multicast route status tests passed.\n";
    return 0;
}

