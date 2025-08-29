// src/test/test_admin_updates.cpp
#include "common/Database.h"
#include <iostream>
#include <cmath>

using namespace transport;

static bool approx(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

int main() {
    Database db;
    if (!db.initialize("test_admin.db")) {
        std::cerr << "DB init failed\n";
        return 1;
    }

    // --- TEST 1: updatePrice ---
    if (!db.updatePrice(VehicleType::BUS, TicketType::INDIVIDUAL, 2.50)) {
        std::cerr << "updatePrice failed\n";
        return 1;
    }
    auto price = db.getPrice(VehicleType::BUS, TicketType::INDIVIDUAL);
    if (!price || !approx(price->base_price, 2.50)) {
        std::cerr << "getPrice failed or wrong value\n";
        return 1;
    }
    std::cout << "updatePrice OK\n";

    // --- TEST 2: updateVehicle ---
    Vehicle v;
    v.uri = "bus001";
    v.type = VehicleType::BUS;
    v.capacity = 50;
    v.available_seats = 50;
    v.route = "A1";
    v.active = true;
    v.last_update = "2025-01-01 10:00:00";
    if (!db.registerVehicle(v)) {
        std::cerr << "registerVehicle failed\n";
        return 1;
    }

    if (!db.updateVehicle("bus001", true, std::string("B2"), VehicleType::BUS)) {
        std::cerr << "updateVehicle failed\n";
        return 1;
    }
    auto veh = db.getVehicle("bus001");
    if (!veh || veh->route != "B2") {
        std::cerr << "getVehicle after updateVehicle failed\n";
        return 1;
    }
    std::cout << "updateVehicle OK\n";

    // --- TEST 3: updateVehicleCapacity ---
    if (!db.updateVehicleCapacity("bus001", 60, 58)) {
        std::cerr << "updateVehicleCapacity failed\n";
        return 1;
    }
    veh = db.getVehicle("bus001");
    if (!veh || veh->capacity != 60 || veh->available_seats != 58) {
        std::cerr << "getVehicle after updateVehicleCapacity failed\n";
        return 1;
    }
    std::cout << "updateVehicleCapacity OK\n";

    std::cout << "All admin update tests passed!\n";
    return 0;
}

