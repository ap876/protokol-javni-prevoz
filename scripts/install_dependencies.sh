#!/bin/bash

# Install dependencies for Transport Protocol System
# This script installs all required packages on Linux systems

set -e

echo "=== Installing Dependencies for Transport Protocol System ==="

# Detect Linux distribution
    if [ -f /etc/os-release ]; then
        . /etc/os-release
    OS=$NAME
    VER=$VERSION_ID
elif type lsb_release >/dev/null 2>&1; then
    OS=$(lsb_release -si)
    VER=$(lsb_release -sr)
elif [ -f /etc/lsb-release ]; then
    . /etc/lsb-release
    OS=$DISTRIB_ID
    VER=$DISTRIB_RELEASE
elif [ -f /etc/debian_version ]; then
    OS=Debian
    VER=$(cat /etc/debian_version)
elif [ -f /etc/SuSe-release ]; then
    OS=openSUSE
elif [ -f /etc/redhat-release ]; then
    OS=RedHat
else
    OS=$(uname -s)
    VER=$(uname -r)
fi

echo "Detected OS: $OS $VER"

# Function to install packages
install_packages() {
    local packages=("$@")
    echo "Installing packages: ${packages[*]}"
    
    if command -v apt-get >/dev/null 2>&1; then
        # Debian/Ubuntu
        sudo apt-get update
        sudo apt-get install -y "${packages[@]}"
    elif command -v yum >/dev/null 2>&1; then
        # CentOS/RHEL/Fedora (older versions)
        sudo yum install -y "${packages[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        # CentOS/RHEL/Fedora (newer versions)
        sudo dnf install -y "${packages[@]}"
    elif command -v pacman >/dev/null 2>&1; then
        # Arch Linux
        sudo pacman -S --noconfirm "${packages[@]}"
    elif command -v zypper >/dev/null 2>&1; then
        # openSUSE
        sudo zypper install -y "${packages[@]}"
    else
        echo "Error: No supported package manager found"
        echo "Please install the following packages manually:"
        printf '%s\n' "${packages[@]}"
        exit 1
    fi
}

# Function to install development tools
install_dev_tools() {
    echo "Installing development tools..."
    
    if command -v apt-get >/dev/null 2>&1; then
        # Debian/Ubuntu
        sudo apt-get update
        sudo apt-get install -y build-essential
    elif command -v yum >/dev/null 2>&1; then
        # CentOS/RHEL/Fedora (older versions)
        sudo yum groupinstall -y "Development Tools"
    elif command -v dnf >/dev/null 2>&1; then
        # CentOS/RHEL/Fedora (newer versions)
        sudo dnf groupinstall -y "Development Tools"
    elif command -v pacman >/dev/null 2>&1; then
        # Arch Linux
        sudo pacman -S --noconfirm base-devel
    elif command -v zypper >/dev/null 2>&1; then
        # openSUSE
        sudo zypper install -y -t pattern devel_C_C++
    else
        echo "Warning: Could not install development tools automatically"
        echo "Please install build tools manually for your distribution"
    fi
}

# Install development tools first
install_dev_tools

# Install core dependencies
echo "Installing core dependencies..."
if command -v apt-get >/dev/null 2>&1; then
    # Debian/Ubuntu
    install_packages cmake libssl-dev libsqlite3-dev pkg-config git
elif command -v yum >/dev/null 2>&1; then
    # CentOS/RHEL/Fedora (older versions)
    install_packages cmake openssl-devel sqlite-devel pkgconfig git
elif command -v dnf >/dev/null 2>&1; then
    # CentOS/RHEL/Fedora (newer versions)
    install_packages cmake openssl-devel sqlite-devel pkgconfig git
elif command -v pacman >/dev/null 2>&1; then
    # Arch Linux
    install_packages cmake openssl sqlite pkg-config git
elif command -v zypper >/dev/null 2>&1; then
    # openSUSE
    install_packages cmake libopenssl-devel sqlite3-devel pkg-config git
fi

# Verify installations
echo ""
echo "=== Verifying Dependencies ==="

check_dependency() {
    local name=$1
    local command=$2
    local package=$3
    
    if command -v "$command" >/dev/null 2>&1; then
        echo "✓ $name is installed"
        if [ "$command" = "cmake" ]; then
            local version
            version=$("$command" --version | head -n1)
            echo "  Version: $version"
        elif [ "$command" = "g++" ]; then
            local version
            version=$("$command" --version | head -n1)
            echo "  Version: $version"
        fi
    else
        echo "✗ $name is NOT installed"
        if [ -n "$package" ]; then
            echo "  Try installing: $package"
        fi
        return 1
    fi
}

# Check all dependencies
all_good=true

echo "Checking build tools..."
check_dependency "CMake" "cmake" "cmake" || all_good=false
check_dependency "G++ Compiler" "g++" "build-essential" || all_good=false

echo ""
echo "Checking development libraries..."
if pkg-config --exists openssl 2>/dev/null; then
    echo "✓ OpenSSL development libraries are installed"
    pkg-config --modversion openssl | xargs echo "  Version:"
else
    echo "✗ OpenSSL development libraries are NOT installed"
    echo "  Try installing: libssl-dev (Ubuntu/Debian) or openssl-devel (CentOS/RHEL)"
    all_good=false
fi

if pkg-config --exists sqlite3 2>/dev/null; then
    echo "✓ SQLite3 development libraries are installed"
    pkg-config --modversion sqlite3 | xargs echo "  Version:"
else
    echo "✗ SQLite3 development libraries are NOT installed"
    echo "  Try installing: libsqlite3-dev (Ubuntu/Debian) or sqlite-devel (CentOS/RHEL)"
    all_good=false
fi

echo ""
if [ "$all_good" = true ]; then
    echo "=== All Dependencies Installed Successfully! ==="
    echo ""
    echo "You can now build the project:"
    echo "  chmod +x scripts/build.sh"
    echo "  ./scripts/build.sh"
else
    echo "=== Some Dependencies Are Missing ==="
    echo ""
    echo "Please install the missing dependencies and run this script again."
    echo "Or try installing manually:"
    echo ""
    if command -v apt-get >/dev/null 2>&1; then
        echo "  sudo apt-get install cmake build-essential libssl-dev libsqlite3-dev"
    elif command -v yum >/dev/null 2>&1; then
        echo "  sudo yum install cmake gcc-c++ openssl-devel sqlite-devel"
    elif command -v dnf >/dev/null 2>&1; then
        echo "  sudo dnf install cmake gcc-c++ openssl-devel sqlite-devel"
    elif command -v pacman >/dev/null 2>&1; then
        echo "  sudo pacman -S cmake base-devel openssl sqlite"
    fi
    exit 1
fi
