#include "common/Database.h"
#include <iostream>
#include <memory>

using namespace transport;

static void print_ok(const char* what, bool ok) {
    std::cout << (ok ? "[OK] " : "[FAIL] ") << what << "\n";
}

int main() {
    // 1) Init pool/DB (lokalni fajl test.db u build folderu)
    auto& pool = DatabasePool::getInstance();
    if (!pool.initialize("test.db", 1)) {
        std::cerr << "DB init failed\n";
        return 1;
    }
    auto db = pool.getConnection();

    // 2) Kreiraj 3 usera (A je lider)
    User a{"1111111111111","Ana",28,"",true,"hashA"};
    User b{"2222222222222","Boris",34,"",true,"hashB"};
    User c{"3333333333333","Ceda",19,"",true,"hashC"};

    print_ok("register Ana",   db->registerUser(a));
    print_ok("register Boris", db->registerUser(b));
    print_ok("register Ceda",  db->registerUser(c));

    // 3) Kreiraj grupu i izaberi *Ana* za lidera
    Group g{};
    g.group_name    = "ekipa_subota";
    g.leader_urn    = a.urn;              // korisnik bira lidera
    g.creation_date = "";                 // DB helper će staviti now
    g.active        = true;

    print_ok("create group ekipa_subota", db->createGroup(g));

    // 4) Provjera lidera
    std::string leader = db->getGroupLeader("ekipa_subota");
    print_ok("leader == Ana", leader == a.urn);

    // 5) Dodaj člana Borisa u grupu (po imenu grupe API)
    print_ok("add Boris to group", db->addUserToGroup(b.urn, "ekipa_subota"));

    // 6) Pokušaj ukloniti Borisa (samo test DB API-ja)
    print_ok("remove Boris from group", db->removeUserFromGroup(b.urn, "ekipa_subota"));

    // 7) Ponovno dodaj Borisa i dodaj Čedu
    print_ok("re-add Boris", db->addUserToGroup(b.urn, "ekipa_subota"));
    print_ok("add Ceda",     db->addUserToGroup(c.urn, "ekipa_subota"));

    // 8) Dummy vozilo i kupovina cijena=1.0 (placeholder)
    Vehicle v{};
    v.uri = "veh-001"; v.type = VehicleType::BUS; v.capacity = 50; v.available_seats = 50;
    v.route = "R1"; v.active = true; v.last_update = "now";
    print_ok("register vehicle", db->registerVehicle(v));

    // Cijena jednog (DB trenutno vraća 1.0)
    double p1 = db->calculateTicketPrice(VehicleType::BUS, TicketType::INDIVIDUAL, 1, 1.0, 30.0);
    std::cout << "price each = " << p1 << "\n";
    print_ok("price == 1.0", p1 == 1.0);

    // 9) Kreiraj jednu kartu Ani
    Ticket t{};
    t.ticket_id="TKT_TEST_1"; t.user_urn=a.urn; t.type=TicketType::INDIVIDUAL; t.vehicle_type=VehicleType::BUS;
    t.route="R1"; t.price=p1; t.discount=0.0; t.purchase_date="now"; t.seat_number="1"; t.used=false;
    print_ok("create ticket", db->createTicket(t));

    // 10) Record payment
    Payment pay{};
    pay.transaction_id="TX_TEST_1"; pay.ticket_id=t.ticket_id; pay.amount=p1;
    pay.payment_method="card"; pay.payment_date="now"; pay.successful=true;
    print_ok("record payment", db->recordPayment(pay));

    pool.returnConnection(db);
    pool.shutdown();
    return 0;
}

