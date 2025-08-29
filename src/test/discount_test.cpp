#include "common/Database.h"
#include <iostream>
#include <cmath>

using namespace transport;

static bool almost_eq(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Inicijalizacija DB pool-a (isti DB kao ostali testovi)
    auto& pool = DatabasePool::getInstance();
    pool.initialize("central_server.db", 1);

    auto db = pool.getConnection();

    // 1) Osnovna cijena (placeholder = 1.0)
    double base_each = db->calculateTicketPrice(VehicleType::BUS, TicketType::INDIVIDUAL, 1, 5.0, 30.0);
    if (almost_eq(base_each, 1.0)) std::cout << "[OK] base price == 1.0\n";
    else                           std::cout << "[FAIL] base price != 1.0 (" << base_each << ")\n";

    // 2) Pravilo: >=3 karte -> 10% popusta
    int passengers = 3;
    double disc_rate3 = db->calculateDiscount("1110000000001", TicketType::INDIVIDUAL, passengers);
    double total3 = base_each * passengers * (1.0 - disc_rate3);
    if (almost_eq(disc_rate3, 0.10)) std::cout << "[OK] 3+ tickets discount = 10%\n";
    else                             std::cout << "[FAIL] 3+ tickets discount wrong (" << disc_rate3 << ")\n";
    if (almost_eq(total3, 3.0 * 0.9)) std::cout << "[OK] total with 3 tickets = 2.7\n";
    else                               std::cout << "[FAIL] total with 3 tickets != 2.7 (" << total3 << ")\n";

    // 3) Family paket -> 10% popusta, recimo 4 osobe
    passengers = 4;
    double disc_family = db->calculateDiscount("2220000000002", TicketType::GROUP_FAMILY, passengers);
    double total_family = base_each * passengers * (1.0 - disc_family);
    if (almost_eq(disc_family, 0.10)) std::cout << "[OK] family package discount = 10%\n";
    else                               std::cout << "[FAIL] family package discount wrong (" << disc_family << ")\n";
    if (almost_eq(total_family, 4.0 * 0.9)) std::cout << "[OK] total for family(4) = 3.6\n";
    else                                     std::cout << "[FAIL] total for family(4) != 3.6 (" << total_family << ")\n";

    // 4) Kontrola: 2 individualne karte -> bez popusta
    passengers = 2;
    double disc_2 = db->calculateDiscount("3330000000003", TicketType::INDIVIDUAL, passengers);
    double total2 = base_each * passengers * (1.0 - disc_2);
    if (almost_eq(disc_2, 0.0)) std::cout << "[OK] 2 tickets no discount\n";
    else                        std::cout << "[FAIL] 2 tickets got discount (" << disc_2 << ")\n";
    if (almost_eq(total2, 2.0)) std::cout << "[OK] total for 2 = 2.0\n";
    else                         std::cout << "[FAIL] total for 2 != 2.0 (" << total2 << ")\n";

    pool.returnConnection(db);
    return 0;
}

