#include "common/Database.h"
#include "common/Logger.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cassert>

using namespace transport;

static void ok(const char* what, bool cond) {
    std::cout << (cond ? "[OK] " : "[FAIL] ") << what << std::endl;
    if (!cond) std::abort();
}

// Jedna rezervacija uz transakciju: read->check->update
// Vraća true ako je uspjela (bilo je slobodnih mjesta), false inače.
static bool try_reserve_one(const std::shared_ptr<Database>& db, const std::string& uri) {
    if (!db->beginTransaction()) return false;

    auto v = db->getVehicle(uri);
    if (!v) {
        db->rollbackTransaction();
        return false;
    }
    if (v->available_seats <= 0) {
        db->rollbackTransaction();
        return false;
    }
    int new_avail = v->available_seats - 1;
    bool ok = db->updateSeatAvailability(uri, new_avail);
    if (!ok) {
        db->rollbackTransaction();
        return false;
    }
    return db->commitTransaction();
}

int main() {
    // Init pool za konkurentni pristup
    auto& pool = DatabasePool::getInstance();
    ok("init db pool", pool.initialize("concurrent_reservation_test.db", 8));

    // 1) Ubaci vozilo sa malim kapacitetom
    {
        auto db = pool.getConnection();
        Vehicle bus{};
        bus.uri             = "bus://42";
        bus.type            = VehicleType::BUS;
        bus.capacity        = 3;   // vrlo malo da lako testiramo “overbook” pokušaje
        bus.available_seats = 3;
        bus.route           = "R_42";
        bus.active          = true;
        bus.last_update     = "2025-01-01 12:00:00";

        // Ako već postoji, ignoriši grešku – ili obriši ranije (ovisno o tvojoj implementaciji)
        db->registerVehicle(bus);
    }

    const std::string uri = "bus://42";

    // 2) Dva thread-a pokušavaju ukupno napraviti više rezervacija nego što je kapacitet
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    auto worker = [&](int attempts){
        auto db = pool.getConnection();
        for (int i=0; i<attempts; ++i) {
            // mala pauza da povećamo šansu za “takmičenje”
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (try_reserve_one(db, uri)) success_count++;
            else                          fail_count++;
        }
    };

    // Svaki thread pokuša npr. 5 rezervacija (zajedno 10), ali kapacitet je 3
    std::thread t1(worker, 5);
    std::thread t2(worker, 5);
    t1.join(); t2.join();

    // 3) Provjeri stanje
    auto db = pool.getConnection();
    auto v = db->getVehicle(uri);
    ok("vehicle exists", (bool)v);

    // Očekujemo da je tačno 3 rezervacije uspjelo (koliko je kapacitet)
    ok("success_count == capacity (3)", success_count == 3);

    // Preostala mjesta idu na 0
    ok("available_seats == 0", v->available_seats == 0);

    // Pokušaj još jednu rezervaciju -> mora pasti
    ok("extra reserve fails", try_reserve_one(db, uri) == false);

    std::cout << "Concurrent reservation test passed.\n";
    return 0;
}

