#!/bin/bash

# Build script for Transport Protocol System
# This script builds the project and generates TLS certificates

set -e

# Get the directory where this script is located and go up to project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "=== Transport Protocol Build Script ==="
echo "Script location: $SCRIPT_DIR"
echo "Project directory: $PROJECT_DIR"
echo "Build directory: $BUILD_DIR"

# Check if we're in the right directory structure
if [ ! -f "$PROJECT_DIR/CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found in project directory!"
    echo "Current working directory: $(pwd)"
    echo "Expected project directory: $PROJECT_DIR"
    echo ""
    echo "Please run this script from the main project directory:"
    echo "  cd /path/to/your_project"
    echo "  ./scripts/build.sh"
    echo ""
    echo "Or if you're in the scripts directory, go up one level:"
    echo "  cd .."
    echo "  ./scripts/build.sh"
    exit 1
fi

# Change to project directory
cd "$PROJECT_DIR"
echo "Changed to project directory: $(pwd)"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check dependencies
echo "Checking dependencies..."

if ! command_exists cmake; then
    echo "Error: cmake not found. Please install cmake:"
    echo "  Ubuntu/Debian: sudo apt-get install cmake"
    echo "  CentOS/RHEL: sudo yum install cmake"
    echo ""
    echo "You can also run the dependency installer:"
    echo "  sudo ./scripts/install_dependencies.sh"
    exit 1
fi

if ! command_exists g++; then
    echo "Error: g++ not found. Please install build tools:"
    echo "  Ubuntu/Debian: sudo apt-get install build-essential"
    echo "  CentOS/RHEL: sudo yum groupinstall 'Development Tools'"
    echo ""
    echo "You can also run the dependency installer:"
    echo "  sudo ./scripts/install_dependencies.sh"
    exit 1
fi

if ! command_exists openssl; then
    echo "Error: openssl not found. Please install openssl:"
    echo "  Ubuntu/Debian: sudo apt-get install libssl-dev"
    echo "  CentOS/RHEL: sudo yum install openssl-devel"
    echo ""
    echo "You can also run the dependency installer:"
    echo "  sudo ./scripts/install_dependencies.sh"
    exit 1
fi

# Check for SQLite3 development libraries using pkg-config
if ! pkg-config --exists sqlite3 2>/dev/null; then
    echo "Error: SQLite3 development libraries not found."
    echo "Please install:"
    echo "  Ubuntu/Debian: sudo apt-get install libsqlite3-dev"
    echo "  CentOS/RHEL: sudo yum install sqlite-devel"
    echo ""
    echo "You can also run the dependency installer:"
    echo "  sudo ./scripts/install_dependencies.sh"
    exit 1
fi

echo "✓ All dependencies found."

# Clean and create build directory
echo "Setting up build directory..."
if [ -d "$BUILD_DIR" ]; then
    echo "Removing previous build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring build with CMake..."
cmake -DCMAKE_BUILD_TYPE=Release "$PROJECT_DIR"

# Build
echo "Building project..."
make -j$(nproc)

echo "✓ Build completed successfully!"

# Create necessary directories
echo "Creating runtime directories..."
mkdir -p "$BUILD_DIR/data"
mkdir -p "$BUILD_DIR/logs"
mkdir -p "$BUILD_DIR/certs"

# Generate TLS certificates
echo "Generating TLS certificates..."
cd "$BUILD_DIR/certs"

# Generate CA certificate
openssl req -x509 -newkey rsa:2048 -keyout ca.key -out ca.crt -days 365 -nodes \
    -subj "/C=BA/ST=FBiH/L=Sarajevo/O=Transport Protocol/CN=CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

# Generate server key
openssl genrsa -out server.key 2048

# Generate server certificate
openssl req -new -key server.key -out server.csr \
    -subj "/C=BA/ST=FBiH/L=Sarajevo/O=Transport Protocol/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    -addext "extendedKeyUsage=serverAuth"

# Sign server certificate with CA
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out server.crt -days 365 \
    -extfile <(echo "extendedKeyUsage=serverAuth") \
    -extfile <(echo "subjectAltName=DNS:localhost,IP:127.0.0.1")

# Clean up temporary files
rm -f server.csr *.srl

# Set proper permissions
chmod 600 *.key
chmod 644 *.crt

# Verify certificates
echo "Verifying certificates..."
if openssl verify -CAfile ca.crt server.crt; then
    echo "✓ Certificate verification successful"
else
    echo "✗ Certificate verification failed"
    exit 1
fi

cd "$PROJECT_DIR"

echo ""
echo "=== Build Summary ==="
echo "✓ Build completed successfully"
echo "✓ TLS certificates generated and verified"
echo ""
echo "Generated files:"
echo "  - build/central_server"
echo "  - build/vehicle_server"
echo "  - build/user_client"
echo "  - build/certs/ca.crt (CA certificate)"
echo "  - build/certs/server.crt (Server certificate)"
echo "  - build/certs/server.key (Server private key)"
echo ""
echo "=== Ready to Run ==="
echo "1. Start central server:"
echo "   ./build/central_server --port 8080 --cert build/certs/server.crt --key build/certs/server.key"
echo ""
echo "2. Start vehicle server:"
echo "   ./build/vehicle_server --port 8081 --cert build/certs/server.crt --key build/certs/server.key"
echo ""
echo "3. Start user client:"
echo "   ./build/user_client --server localhost --port 8080 --ca build/certs/ca.crt"
echo ""
