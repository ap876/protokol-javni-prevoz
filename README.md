# Public Transport Protocol System - Setup and Run Instructions

## Overview
Transport Protocol System is a C++ project that implements a secure public transport protocol across three components: the Central Server (users, authentication, reservations, and purchases), the Vehicle Server (route state, capacity, and reservation confirmations), and the User Client (an interactive CLI for registration, login, reservations, purchases, and group management). The goal is to provide a robust and verifiable end-to-end flow—from user registration to ticket issuance and validation.
Communication between components uses TLS over TCP/IP, and application messages are JSON/UTF-8. The networking layer is based on Boost.Asio with OpenSSL and boost::asio::ssl::stream for TLS.

## System Architecture

```
┌─────────────────┐    ┌─────────────────┐
│  Central Server │    │  Vehicle Server │
│   Port: 8080    │◄───┤   Port: 8081    │
│                 │    │                 │
│ • User Mgmt     │    │ • Route Mgmt    │
│ • Authentication│    │ • Seat Tracking │
│ • Coordination  │    │ • Payments      │
└─────────────────┘    └─────────────────┘
         ▲                       ▲
         │                       │
         └───────────────────────┘
                    │
         ┌─────────────────┐
         │   User Client   │
         │                 │
         │ • Interactive   │
         │ • Commands      │
         │ • TLS Secure    │
         └─────────────────┘
```

## Prerequisites
- Linux system (Ubuntu, CentOS, RHEL, Fedora, Arch, or openSUSE)
- Internet connection for downloading dependencies
- Sudo privileges for installing packages

## Installation Steps

### Step 1: Install Boost.Asio 
Install Boost development packages for your distribution:

- **Ubuntu / Debian**
  sudo apt-get update
  sudo apt-get install -y libboost-all-dev
  **Minimal alternative:**
  **sudo apt-get install -y libboost-system-dev libboost-thread-dev**

- **Fedora**
  sudo dnf install -y boost-devel

- **RHEL / CentOS (8+)**
  sudo yum install -y boost-devel
  
- **Arch Linux**
  sudo pacman -S --noconfirm boost

- **openSUSE**
  sudo zypper install -y boost-devel

### Step 2: Install Dependencies

```bash
# Make scripts executable
chmod +x scripts/*.sh

# Install all required dependencies automatically
sudo ./scripts/install_dependencies.sh
```

This will install:
- Build tools (gcc, g++, make, cmake)
- OpenSSL development libraries
- SQLite3 development libraries
- Git and other utilities

### Step 3: Build the System

```bash
# Build the complete system
./scripts/build.sh

# For debug build with additional checks
./scripts/build.sh --debug

# For clean build (removes previous build)
./scripts/build.sh --clean
```

This script will:
- Build all executables (central_server, vehicle_server, user_client)
- Generate TLS certificates automatically
- Verify the build and certificates
- Create necessary runtime directories

## File Locations

After building, the system creates:
```
build/
├── central_server      # Central server executable
├── vehicle_server      # Vehicle server executable
├── user_client         # User client executable
├── certs/              # TLS certificates
│   ├── ca.crt          # CA certificate
│   ├── server.crt      # Server certificate
│   └── server.key      # Server private key
├── data/               # Database files
└── logs/               # Log files
```

### Step 4: Run the System

**4.1. Multi-VM Testing (two nodes)**

- Server VM (Central + Vehicle): SERVER_IP 
- Client VM (User Client): CLIENT_IP 

**VM1 (SERVER_IP) – terminal 1: Central Server**
```bash
cd build
./central_server --port 8080 \
  --cert certs/server.crt --key certs/server.key
```
**VM1 (SERVER_IP) – terminal 2: Vehicle Server (bus)**
```bash
cd build
./vehicle_server --type bus --port 8081 \
  --central-server SERVER_IP:8080 \
  --cert certs/server.crt --key certs/server.key
```
**VM2 (CLIENT_IP): User Client**
```bash
cd build
./user_client --server SERVER_IP --port 8080 --ca certs/ca.crt

TLS quick check (optional, sa Client VM-a):

openssl s_client -connect SERVER_IP:8080 -servername SERVER_IP  | head -n 20
```
### 4.2 Single Machine Testing
You'll need **3 terminal windows** to run the complete system:

#### Terminal 1: Start Central Server
```bash
./build/central_server --port 8080 
```
**Expected output:**
```
[INFO] [CentralServer] Starting Central Server...
[INFO] [CentralServer] Port: 8080
[INFO] [CentralServer] Database: central_server.db
[INFO] [CentralServer] Certificate: certs/server.crt
[INFO] [CentralServer] Key: certs/server.key
[INFO] [CentralServer] Multicast: OFF addr=239.192.0.1 port=30001
[INFO] [CentralServer] Starting server on port 8080...
[INFO] [CentralServer] Central Server started on port 8080
[INFO] [CentralServer] Central Server started successfully
Central Server is running on port 8080
Press Ctrl+C to stop the server

```
#### Terminal 2: Start Vehicle Server
```bash
./build/vehicle_server --type bus --port 8081 --central-server localhost:8080
```
**Expected output:**
```
[INFO] [VehicleServer] Starting Vehicle Server (bus)...
[INFO] [VehicleServer] Port: 8081
[INFO] [VehicleServer] Central Server: localhost:8080
[INFO] [VehicleServer] Certificate: certs/server.crt
[INFO] [VehicleServer] Key: certs/server.key
[INFO] [VehicleServer] Starting bus server on port 8081...
[INFO] [VehicleServer] Vehicle Server started on port 8081
[INFO] [VehicleServer] Vehicle Server (bus) started successfully
bus Server is running on port 8081
Connected to Central Server: localhost:8080
Press Ctrl+C to stop the server

```
#### Terminal 3: Start User Client
```bash
./build/user_client --server localhost --port 8080

```

**Expected output:**
```
[INFO] [UserClient] Starting User Client...
[INFO] [UserClient] Server: localhost:8080
[INFO] [UserClient] CA: certs/ca.crt
[INFO] [UserClient] Connecting to server...
[INFO] [UserInterface] Connected to server successfully
[INFO] [UserClient] Connected to server successfully
Connected to Central Server at localhost:8080

=== Public Transport System Client ===
Type 'help' for available commands or 'quit' to exit

Transport Protocol Client - Interactive Session
Type 'help' for available commands

transport>
```
## Using the User Client

Once the client is connected, you can use these commands:

### Basic Commands
```
help                    - Show available commands
quit                    - Exit the client
```

### User Management
```
register 1234567890123 - Register with 13-digit URN
authenticate 1234567890123 - Authenticate with URN
```
### Vehicle / Route Binding & Events
```
register_device uri=bus1 bus - Register/bind a target vehicle/route (URI) for mode 'bus'
register_device uri=tram2 tram - Register/bind a target vehicle/route (URI) for mode 'tram'
register_device uri=trolleybus3 trolleybus - Register/bind a target vehicle/route (URI) for mode 'trolleybus'
```
### Ticket Operations
```
reserve bus bus125 - Reserve a seat on vehicle/route 'bus125' (mode: bus)
purchase individual bus uri=bus125 2 - Purchase 2 individual tickets for 'bus125' (mode: bus)
```
### Group Management
```
create_group ekipa 1234567890123 - Create group 'ekipa' with leader URN
add_member ekipa 9876543210987 - Add member (URN) to group 'ekipa'
rm_member ekipa 9876543210987 - Remove member (URN) from group 'ekipa'
```

### Multicast communication
```
cd build

# Start Central Server
./central_server --bind SERVER_IP --port 8080

# Start Vehicle Server (in this case we used type = bus)
./vehicle_server --type bus --port 8081 --central-server SERVER_IP:8080  

# Start Client A (monitor / listen)
./user_client -id A --server SERVER_IP --port 8080 listen

# Start Client B
./user_client -id B --server SERVER_IP --port 8080

# Start Client C
./user_client -id C --server SERVER_IP --port 8080

```
### Upravljanje bazom podataka
```
cd build

#Start database
sqlite3 central_server.db

#Users
-- Number of registered users
SELECT COUNT(*) FROM users;

-- List all users (URN + status)
SELECT urn, active FROM users;

#Vehicles
-- Number of registered vehicles
SELECT COUNT(*) FROM vehicles;

-- List all vehicles
SELECT * FROM vehicles;

-- Active vehicles only
SELECT COUNT(*) FROM vehicles WHERE active=1;

#Tickets
-- Number of purchased tickets
SELECT COUNT(*) FROM tickets;

-- List all purchased tickets
SELECT * FROM tickets;

```
### General
```
help - Show available commands
quit - Exit the client
```

**Notes**
- **URN** must be 13 digits (e.g., `1234567890123`).
- **Modes** currently supported: `bus` (extend with `tram`, `trolleybus` if enabled).
- **URI** (e.g., `bus125`) identifies a concrete vehicle/route; use `register_device` before reserving/purchasing if your workflow requires a bound target.


## Step 4.3 Running Tests

### Protocol Tests
End-to-end check of the core protocol: register, authenticate, register device, reserve, and purchase (happy path).
./protocol_test

### Basic Test
Minimal “smoke” test: server boots and basic commands work without errors.
./basic_test

### Benchmark Tests
Throughput/latency measurement under load -> example: 50 connections for 60 seconds.
./benchmark_test --connections 50 --duration 60

### Test Three Clients
The test verifies that a leader can create a group and two other users can concurrently join it, while ensuring correct behavior for adding, removing, and re-adding members under concurrent access conditions.
./test_three_clients

### Concurrent Reservation Test
Verifies race control for reservations: prevents double-booking and checks.
./concurrent_reservation_test

### Discount Test
Validates discount/pricing logic per ticket/group type (individual/family/business/tourist).
./discount_test

### Group Duplicate Member Test
Validates group membership rules: prevents adding the same member twice and checks leader/member constraints.
./group_duplicate_member_test

### Admin Policy Test
Validates admin-only operations and expected error codes for non-admin users.
./admin_policy_test

### Test Admin Updates
Verifies that admin changes (e.g., fares/routes) propagate to clients and are persisted (DB + notifications).
./test_admin_updates

### Stream Test
Robust TCP framing: handles fragmentation/reassembly; checks length-prefix and CRC32 integrity.
./stream_test

### TLS Test
TLS handshake and secure connection (Boost.Asio/OpenSSL); rejects invalid certificates per default config.
./tls_test

### Multicast Test
Low-level UDP multicast: join/leave group and receive announcements (service discovery/announce).
./mcast_test

### Route Status Multicast Test
Asynchronous broadcasting of line/route status and reception by subscribed clients.
./route_status_mcast_test

### Performance
Performance profiling with `perf`:
cd build

# Start central server
sudo perf record -g ./central_server

# Start client
./user_client --server localhost --port 8080

# Checking report
sudo perf report

## Troubleshooting

###Common Issues and Solutions

#### 1. TLS Handshake Failure
**Error:** `TLS handshake failed: sslv3 alert handshake failure`

**Solution:**
```bash
# Rebuild the project to regenerate certificates
./scripts/build.sh
```

#### 2. "Permission Denied" Errors
**Solution:**
```bash
# Make scripts executable
chmod +x scripts/*.sh
```

#### 3. "Command Not Found" Errors
**Solution:**
```bash
# Install dependencies
sudo ./scripts/install_dependencies.sh
```
#### 4. Certificate Errors
**Error:** `Failed to load certificate` or `Failed to load CA certificate`

**Solution:**
```bash
# Verify certificates exist
ls -la build/certs/

# Rebuild if certificates are missing
./scripts/build.sh
```
### 6. UPPAAL
```
#Download 
https://uppaal.org/downloads/

#Starting UPPAAL on linux
./uppaal
  ```


