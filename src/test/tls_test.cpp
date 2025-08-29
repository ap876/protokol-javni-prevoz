#include "common/Message.h"
#include "common/TLSSocket.h"
#include "common/TLSServer.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace transport;

// mini helper za ispis i abort
static void ok(const char* what, bool cond) {
    std::cout << (cond ? "[OK] " : "[FAIL] ") << what << std::endl;
    if (!cond) std::abort();
}

// Vrati random port u “user” rangeu (ephemeral)
static int pick_port() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(20000, 40000);
    return dis(gen);
}

int main() {
    const std::string cert_path = "certs/server.crt";
    const std::string key_path  = "certs/server.key";

    // -------- 1) Startaj TLS echo server --------
    int port = pick_port();
    std::atomic<bool> server_ready{false};
    std::atomic<bool> got_one{false};

    TLSServer server;
    server.setConnectionCallback([&](std::unique_ptr<TLSSocket> client) {
        
        std::thread([c = std::move(client), &got_one]() mutable {
            // primi 1 poruku i pošalji odgovor
            auto msg = c->receiveMessage();
            if (msg) {
                // mala provjera sadržaja
                bool same = (msg->getType() == MessageType::CONNECT_REQUEST &&
                             msg->getString("client_id") == "tls-test-client");

                // vrati RESPONSE_SUCCESS sa echo poljem
                auto resp = MessageFactory::createSuccessResponse(
                    same ? "OK" : "NOK",
                    {{"echo_client_id", msg->getString("client_id")}}
                );
                c->sendMessage(*resp);
                got_one = true;
            }
        }).detach();
    });

    bool s = server.start(port, cert_path, key_path);
    ok("TLS server start()", s);
    server_ready = s;

    // -------- 2) TLS klijent konekcija + poruka --------
    // Napomena: pretpostavka je da TLSSocket zna napraviti TLS klijent koji
    //           vjeruje ovom certu. Ako tvoj TLSSocket očekuje zaseban CA ili
    //           “insecure” mod, prilagodi ove dvije linije po potrebi.
    auto client = std::make_unique<TLSSocket>();
    // ako tvoj TLSSocket ima opciju “skip verify”, koristi npr:
    // client->setInsecureSkipVerify(true);

    // Daj serveru trunku vremena da se digne
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool c = client->connect("127.0.0.1", port/* server CA ili cert za trust */);
    ok("TLS client connect()", c);

    auto m = MessageFactory::createConnectRequest("tls-test-client");
    m->calculateChecksum();
    bool sent_ok = client->sendMessage(*m);
    ok("client sendMessage()", sent_ok);

    auto resp = client->receiveMessage();
    ok("client receiveMessage()", (bool)resp);
    if (resp) {
        ok("resp type == RESPONSE_SUCCESS", resp->getType() == MessageType::RESPONSE_SUCCESS);
        ok("resp echo field", resp->getString("echo_client_id") == "tls-test-client");
        ok("resp message text", resp->getString("message") == "OK");
    }

    // -------- 3) sanity: server je zaista obradio jednu poruku --------
    // (malo pričekaj dok echo thread odradi)
    for (int i=0; i<20 && !got_one.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok("server handled one message", got_one.load());

    // -------- 4) cleanup --------
    server.stop();

    std::cout << "TLS test passed.\n";
    return 0;
}

