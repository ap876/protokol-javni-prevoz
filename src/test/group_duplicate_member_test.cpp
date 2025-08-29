#include "common/Database.h"
#include "common/Logger.h"
#include <iostream>
#include <vector>
#include <cassert>

using namespace transport;

static void ok(const char* what, bool cond) {
    std::cout << (cond ? "[OK] " : "[FAIL] ") << what << std::endl;
    if (!cond) std::abort();
}

int main() {
    // Init DB pool (privremena test baza)
    auto& pool = DatabasePool::getInstance();
    ok("init db pool", pool.initialize("group_dupe_member_test.db", 3));

    auto db = pool.getConnection();
    ok("db open", db && db->isOpen());


    // 1) Kreiraj korisnike
    User ana  {"1111111111111","Ana",  24, "2025-01-01 10:00:00", true, "h"};
    User boris{"2222222222222","Boris",28, "2025-01-01 10:02:00", true, "h"};
    ok("register Ana",  db->registerUser(ana));
    ok("register Boris",db->registerUser(boris));

    // 2) Kreiraj grupu
    Group g{};
    g.group_name    = "moja_grupa";
    g.leader_urn    = ana.urn;
    g.creation_date = "2025-01-01 11:00:00";
    g.active        = true;
    ok("create group", db->createGroup(g));

    // 3) Dodaj Boris-a u grupu (prvi put -> OK)
    ok("add Boris first time", db->addUserToGroup(boris.urn, g.group_name));

    // 4) Pokušaj dodati opet (duplikat) -> očekujemo false (ili da ostane bez duplikata)
    bool added_second_time = db->addUserToGroup(boris.urn, g.group_name);
    ok("add Boris second time rejected", added_second_time == false);

    // 5) Provjeri da Boris nije dupliran (npr. getUserGroups + brojanje članova ako imaš API)
    // Ovdje jednostavnije: pokušaj ga ukloniti jednom -> treba proći,
    // drugi put uklanjanje treba da padne jer ga više nema.
    ok("remove Boris once", db->removeUserFromGroup(boris.urn, g.group_name));
    ok("remove Boris again rejected", db->removeUserFromGroup(boris.urn, g.group_name) == false);

    std::cout << "Group duplicate member test passed.\n";
    return 0;
}

