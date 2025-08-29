#include "server/CentralServer.h"
#include "common/Database.h"
#include <iostream>

using namespace transport;

int main() {
    // Inicijalizuj pool ako nije već 
    auto& pool = DatabasePool::getInstance();
    pool.initialize("central_server.db", 1);

    // Pripremi test korisnika kojeg ćemo pokušati obrisati
    const std::string urn_del = "9990000000001";
    {
        auto db = pool.getConnection();
        if (!db->getUser(urn_del)) {
            User u{};
            u.urn               = urn_del;
            u.name              = "DeleteMe";
            u.age               = 30;
            u.registration_date = "2025-01-01 12:00:00";
            u.active            = true;
            u.pin_hash          = "hash";
            db->registerUser(u);
        }
        pool.returnConnection(db);
    }

    CentralServer cs; // ne startamo mrežni server, koristimo samo business logiku

    // 1) Pokušaj brisanja bez admin odobrenja -> MORA pasti
    bool user_try_delete = cs.processUserDeletion(urn_del, /*admin_approved=*/false);
    if (!user_try_delete) std::cout << "[OK] user deletion blocked without admin approval\n";
    else                  std::cout << "[FAIL] user deletion passed without admin approval\n";

    // 2) Provjeri da korisnik i dalje postoji
    {
        auto db = pool.getConnection();
        bool still_there = static_cast<bool>(db->getUser(urn_del));
        pool.returnConnection(db);
        if (still_there) std::cout << "[OK] user still exists after blocked deletion\n";
        else             std::cout << "[FAIL] user missing after blocked deletion\n";
    }

    // 3) Admin odobrava -> brisanje treba proći
    bool admin_delete = cs.processUserDeletion(urn_del, /*admin_approved=*/true);
    if (admin_delete) std::cout << "[OK] admin-approved deletion succeeded\n";
    else              std::cout << "[FAIL] admin-approved deletion failed\n";

    // 4) Provjera da je obrisan
    {
        auto db = pool.getConnection();
        bool gone = !static_cast<bool>(db->getUser(urn_del));
        pool.returnConnection(db);
        if (gone) std::cout << "[OK] user really deleted after admin approval\n";
        else      std::cout << "[FAIL] user still exists after admin approval\n";
    }

    return 0;
}

