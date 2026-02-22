#!/bin/bash

# ===========================================
# Raspberry Pi BLE Server - Dependency Installer
# ===========================================

echo "=========================================="
echo "Installing BLE Server Dependencies"
echo "=========================================="

# Exit on error
set -e

# Check if running as root or with sudo
if [ "$EUID" -ne 0 ]; then 
    echo "Please run with sudo: sudo ./install_dependencies.sh"
    exit 1
fi

# Get the actual username (not root when using sudo)
ACTUAL_USER=${SUDO_USER:-$USER}

echo ""
echo "[1/6] Updating system packages..."
echo "----------------------------------------"
apt-get update
apt-get upgrade -y

echo ""
echo "[2/6] Installing build tools..."
echo "----------------------------------------"
apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git

echo ""
echo "[3/6] Installing Bluetooth libraries..."
echo "----------------------------------------"
apt-get install -y \
    bluez \
    bluez-tools \
    libbluetooth-dev \
    libdbus-1-dev \
    libglib2.0-dev

echo ""
echo "[4/6] Installing additional utilities..."
echo "----------------------------------------"
apt-get install -y \
    libreadline-dev \
    pi-bluetooth

echo ""
echo "[5/6] Configuring Bluetooth service..."
echo "----------------------------------------"

# Enable and start Bluetooth service
systemctl enable bluetooth
systemctl start bluetooth

# Enable Bluetooth at boot
systemctl enable hciuart 2>/dev/null || true

# Configure Bluetooth adapter
echo ""
echo "Configuring Bluetooth adapter..."

# Wait for adapter to be ready
sleep 2

# Power on and configure the adapter
hciconfig hci0 up 2>/dev/null || true
hciconfig hci0 piscan 2>/dev/null || true
hciconfig hci0 leadv 3 2>/dev/null || true

# Set device name
DEVICE_NAME="Rover-01"
hciconfig hci0 name "$DEVICE_NAME" 2>/dev/null || true

echo ""
echo "[6/6] Setting permissions..."
echo "----------------------------------------"

# Add user to bluetooth group
usermod -a -G bluetooth $ACTUAL_USER

# Create udev rule for Bluetooth access without root
cat > /etc/udev/rules.d/50-bluetooth-hci.rules << 'EOF'
# Allow users in bluetooth group to access HCI devices
KERNEL=="hci[0-9]*", GROUP="bluetooth", MODE="0660"
EOF

# Reload udev rules
udevadm control --reload-rules
udevadm trigger

# Configure bluetoothd for LE advertising
# Check if ExperimentalFeatures line exists, if not add it
BTCONF="/etc/bluetooth/main.conf"
if [ -f "$BTCONF" ]; then
    if grep -q "^#.*Experimental" "$BTCONF"; then
        sed -i 's/^#.*Experimental.*/Experimental = true/' "$BTCONF"
    elif ! grep -q "Experimental" "$BTCONF"; then
        echo "" >> "$BTCONF"
        echo "[General]" >> "$BTCONF"
        echo "Experimental = true" >> "$BTCONF"
    fi
fi

# Restart Bluetooth to apply changes
systemctl restart bluetooth

echo ""
echo "=========================================="
echo "Installation Complete!"
echo "=========================================="
echo ""
echo "IMPORTANT NOTES:"
echo "----------------------------------------"
echo "1. You may need to REBOOT for all changes to take effect:"
echo "   sudo reboot"
echo ""
echo "2. After reboot, verify Bluetooth is working:"
echo "   hciconfig -a"
echo ""
echo "3. To build the project, run:"
echo "   cd ~/Documents/sensor_ble_server"
echo "   mkdir build"
echo "   cd build"
echo "   cmake .."
echo "   make"
echo ""
echo "4. To run the server:"
echo "   sudo ./sensor_ble_server"
echo ""
echo "=========================================="