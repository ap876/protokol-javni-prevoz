cmake_minimum_required(VERSION 3.16)
project(PublicTransportProtocol VERSION 1.0.0 LANGUAGES CXX)

# ============================================================
# C++ standard i optimizacije
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DDEBUG -fsanitize=address")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -march=native")
endif()

add_definitions(-D_GNU_SOURCE -D_REENTRANT)

# ============================================================
# Paketi
# ============================================================
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# SQLite3
find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIBRARY sqlite3)
if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "SQLite3 not found. Please install libsqlite3-dev")
endif()

# ============================================================
# Include direktoriji
# ============================================================
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${SQLITE3_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})

# ============================================================
# Izvori
# ============================================================
set(COMMON_SOURCES
    src/common/Message.cpp
    src/common/Protocol.cpp
    src/common/Database.cpp
    src/common/TLSSocket.cpp
    src/common/TLSServer.cpp     # <── Asio TLS server
    src/common/Logger.cpp
    src/common/Utils.cpp
)

set(SERVER_SOURCES
    src/server/CentralServer.cpp
    src/server/VehicleServer.cpp
    src/server/AdminServer.cpp
    src/server/ServerBase.cpp
)

set(CLIENT_SOURCES
    src/client/PaymentDevice.cpp
    src/client/ClientBase.cpp
    src/client/UserInterface.cpp
)

# ============================================================
# Biblioteke
# ============================================================
add_library(transport_common STATIC ${COMMON_SOURCES})
target_link_libraries(transport_common
    ${SQLITE3_LIBRARY}
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_definitions(transport_common PRIVATE -DBOOST_ASIO_NO_DEPRECATED)

add_library(transport_server STATIC ${SERVER_SOURCES})
target_link_libraries(transport_server transport_common)

add_library(transport_client STATIC ${CLIENT_SOURCES})
target_link_libraries(transport_client transport_common)

# ============================================================
# Izvršni fajlovi
# ============================================================
add_executable(central_server src/main/central_server_main.cpp)
target_link_libraries(central_server transport_server)

add_executable(vehicle_server src/main/vehicle_server_main.cpp)
target_link_libraries(vehicle_server transport_server)

add_executable(admin_server src/main/admin_server_main.cpp)
target_link_libraries(admin_server transport_server)

add_executable(payment_device src/main/payment_device_main.cpp)
target_link_libraries(payment_device transport_client)

add_executable(user_client src/main/user_client_main.cpp)
target_link_libraries(user_client transport_client)

# Test executables (ako ih koristiš)
add_executable(protocol_test src/test/protocol_test.cpp)
target_link_libraries(protocol_test transport_common transport_server transport_client)

add_executable(benchmark_test src/test/benchmark_test.cpp)
target_link_libraries(benchmark_test transport_common transport_server transport_client)

add_executable(admin_policy_test src/test/admin_policy_test.cpp)
target_link_libraries(admin_policy_test transport_common transport_server transport_client)

add_executable(basic_test src/test/basic_test.cpp)
target_link_libraries(basic_test transport_common transport_server transport_client)

add_executable(discount_test src/test/discount_test.cpp)
target_link_libraries(discount_test transport_common transport_server transport_client)

add_executable(stream_test src/test/stream_test.cpp)
target_link_libraries(stream_test transport_common)

add_executable(mcast_test src/test/mcast_test.cpp)
target_link_libraries(mcast_test transport_common)

add_executable(route_status_mcast_test src/test/route_status_mcast_test.cpp)
target_link_libraries(route_status_mcast_test transport_common)

add_executable(group_duplicate_member_test src/test/group_duplicate_member_test.cpp)
target_link_libraries(group_duplicate_member_test transport_common transport_server transport_client)

add_executable(concurrent_reservation_test src/test/concurrent_reservation_test.cpp)
target_link_libraries(concurrent_reservation_test transport_common transport_server transport_client)

add_executable(tls_test src/test/tls_test.cpp)
target_link_libraries(tls_test transport_common transport_server transport_client OpenSSL::SSL OpenSSL::Crypto)

add_executable(test_admin_updates src/test/test_admin_updates.cpp)
target_link_libraries(test_admin_updates transport_common sqlite3)

add_executable(test_three_clients src/test/test_three_clients.cpp)
target_link_libraries(test_three_clients transport_common sqlite3)

# ============================================================
# Install
# ============================================================
install(TARGETS central_server vehicle_server admin_server payment_device user_client
    RUNTIME DESTINATION bin
)

# ============================================================
# Runtime direktoriji i konfiguracije
# ============================================================
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/data)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/certs)

configure_file(${CMAKE_SOURCE_DIR}/config/server.conf ${CMAKE_BINARY_DIR}/server.conf COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/config/client.conf ${CMAKE_BINARY_DIR}/client.conf COPYONLY)

# ============================================================
# Info
# ============================================================
message(STATUS "=== Linux Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "SQLite3 library: ${SQLITE3_LIBRARY}")
message(STATUS "Boost found: ${Boost_FOUND}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Target system: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")

