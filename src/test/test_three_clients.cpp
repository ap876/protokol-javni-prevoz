// src/test/test_three_clients.cpp
#include "common/Database.h"
#include <sqlite3.h>

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdio>   // std::remove

using namespace transport;

int main() {
    // 0) čista baza
    std::remove("test_three_clients.db");

    // 1) pool (više konekcija)
    if (!DatabasePool::getInstance().initialize("test_three_clients.db", 4)) {
        std::cerr << "[FAIL] DatabasePool initialize\n";
        return 1;
    }

    // 2) korisnici
    const std::string leader = "1000000000001";
    const std::string u2     = "1000000000002";
    const std::string u3     = "1000000000003";

    // 3) sekvencijalna registracija (izbjegne SQLITE_BUSY tokom kreiranja)
    {
        auto db = DatabasePool::getInstance().getConnection();

        User ul{}; ul.urn = leader; ul.name="Leader"; ul.age=30; ul.registration_date="2025-01-01 10:00:00"; ul.active=true; ul.pin_hash="h";
        if (!db->registerUser(ul)) { std::cerr << "[FAIL] registerUser(leader): " << db->getLastError() << "\n"; return 1; }

        User u_2{}; u_2.urn = u2; u_2.name="U2"; u_2.age=22; u_2.registration_date="2025-01-01 10:02:00"; u_2.active=true; u_2.pin_hash="h2";
        if (!db->registerUser(u_2)) { std::cerr << "[FAIL] registerUser(u2): " << db->getLastError() << "\n"; return 1; }

        User u_3{}; u_3.urn = u3; u_3.name="U3"; u_3.age=23; u_3.registration_date="2025-01-01 10:03:00"; u_3.active=true; u_3.pin_hash="h3";
        if (!db->registerUser(u_3)) { std::cerr << "[FAIL] registerUser(u3): " << db->getLastError() << "\n"; return 1; }

        DatabasePool::getInstance().returnConnection(db);
        std::cout << "[OK] Users registered sequentially\n";
    }

    // 4) barijera za grupu
    std::mutex mtx;
    std::condition_variable cv;
    bool group_created = false;

    // helper: retry za addUserToGroup kad je DB busy
    auto add_with_retry = [&](const std::string& who) -> bool {
        for (int attempt = 0; attempt < 8; ++attempt) {
            auto db = DatabasePool::getInstance().getConnection();
            bool ok = db->addUserToGroup(who, "TEAM1");
            int  ec = db->getLastErrorCode();
            std::string em = db->getLastError();
            DatabasePool::getInstance().returnConnection(db);

            if (ok) return true;
            if (ec == SQLITE_BUSY || em.find("busy") != std::string::npos) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150 * (attempt + 1)));
                continue;
            }
            // drugi razlog greške -> nema svrhe retry
            std::cerr << "[addUserToGroup fail] " << who << ": " << em << " (code=" << ec << ")\n";
            return false;
        }
        std::cerr << "[addUserToGroup fail] " << who << ": still busy after retries\n";
        return false;
    };

    // 5) T1 kreira grupu
    std::thread t1([&]{
        auto db = DatabasePool::getInstance().getConnection();

        Group g{};
        g.group_name = "TEAM1";
        g.leader_urn = leader;
        g.creation_date = "2025-01-01 10:10:00";
        g.active = true;

        if (!db->createGroup(g)) {
            std::cerr << "[FAIL] createGroup(TEAM1): " << db->getLastError() << "\n";
            std::exit(1);
        }

        {
            std::lock_guard<std::mutex> lk(mtx);
            group_created = true;
        }
        cv.notify_all();

        DatabasePool::getInstance().returnConnection(db);
        std::cout << "[T1] TEAM1 created\n";
    });

    // 6) T2 i T3 čekaju grupu pa dodaju članove (sa retry)
    std::thread t2([&]{
        { std::unique_lock<std::mutex> lk(mtx); cv.wait(lk, [&]{ return group_created; }); }
        if (!add_with_retry(u2)) std::exit(1);
        std::cout << "[T2] U2 added to TEAM1\n";
    });

    std::thread t3([&]{
        { std::unique_lock<std::mutex> lk(mtx); cv.wait(lk, [&]{ return group_created; }); }
        if (!add_with_retry(u3)) std::exit(1);
        std::cout << "[T3] U3 added to TEAM1\n";
    });

    t1.join(); t2.join(); t3.join();

    // 7) provjere
    {
        auto db = DatabasePool::getInstance().getConnection();

        const std::string leader_db = db->getGroupLeader("TEAM1");
        if (leader_db != leader) {
            std::cerr << "[FAIL] getGroupLeader expected " << leader << " got " << leader_db << "\n";
            return 1;
        }
        std::cout << "[OK] Leader of TEAM1 is " << leader_db << "\n";

        // ukloni u3
        if (!db->removeUserFromGroup(u3, "TEAM1")) {
            std::cerr << "[FAIL] removeUserFromGroup(u3): " << db->getLastError() << "\n";
            return 1;
        }
        std::cout << "[OK] Removed " << u3 << " from TEAM1\n";

        // opet uklanjanje u3 mora pasti (nije više član)
        if (db->removeUserFromGroup(u3, "TEAM1")) {
            std::cerr << "[FAIL] Re-remove unexpectedly succeeded\n";
            return 1;
        } else {
            std::cout << "[OK] Re-remove correctly failed for " << u3 << "\n";
        }

        // ponovo dodaj u3
        if (!add_with_retry(u3)) {
            std::cerr << "[FAIL] Re-add u3: " << db->getLastError() << "\n";
            return 1;
        }
        std::cout << "[OK] Re-added " << u3 << " to TEAM1\n";

        // duplo dodavanje u2 mora pasti (jer je već član)
        if (db->addUserToGroup(u2, "TEAM1")) {
            std::cerr << "[FAIL] Double-add unexpectedly succeeded for u2\n";
            return 1;
        } else {
            std::cout << "[OK] Double-add correctly failed for u2\n";
        }

        DatabasePool::getInstance().returnConnection(db);
    }

    DatabasePool::getInstance().shutdown();
    std::cout << "All three-client tests passed!\n";
    return 0;
}

