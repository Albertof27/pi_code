#include <iostream>
#include <iomanip>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "config.hpp"
#include "sensor_data.hpp"

// Socket path for communication with Python BLE server
#define SOCKET_PATH "/tmp/rover_sensor.sock"

// Global flag for shutdown
std::atomic<bool> g_running(true);

// Signal handler
void signalHandler(int signum) {
    std::cout << "\n[Main] Received signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

// Print startup banner
void printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║     Raspberry Pi Sensor Publisher (C++)               ║" << std::endl;
    std::cout << "║     Sends data to Python BLE Server                   ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n";
}

// Print configuration
void printConfig() {
    std::cout << "Configuration:" << std::endl;
    std::cout << "├─ Socket Path:     " << SOCKET_PATH << std::endl;
    std::cout << "├─ Device Name:     " << Config::DEVICE_NAME << std::endl;
    std::cout << "├─ Service UUID:    " << Config::SERVICE_UUID << std::endl;
    std::cout << "├─ Weight Char:     " << Config::WEIGHT_CHAR_UUID << std::endl;
    std::cout << "├─ Events Char:     " << Config::EVENTS_CHAR_UUID << std::endl;
    std::cout << "├─ Weight Range:    " << Config::WEIGHT_MIN << " - " << Config::WEIGHT_MAX << " lb" << std::endl;
    std::cout << "├─ Overweight At:   " << Config::WEIGHT_THRESHOLD << " lb" << std::endl;
    std::cout << "└─ Update Interval: " << Config::WEIGHT_UPDATE_INTERVAL_MS << " ms" << std::endl;
    std::cout << "\n";
}

// Print event flags in human-readable format
std::string eventFlagsToString(uint16_t flags) {
    if (flags == 0) {
        return "NONE";
    }
    
    std::string result;
    if (flags & Config::EVENT_OVERWEIGHT) {
        result += "OVERWEIGHT ";
    }
    if (flags & Config::EVENT_OUT_OF_RANGE) {
        result += "OUT_OF_RANGE ";
    }
    if (flags & Config::EVENT_LOW_BATTERY) {
        result += "LOW_BATTERY ";
    }
    if (flags & Config::EVENT_ERROR) {
        result += "ERROR ";
    }
    
    return result;
}

// Socket publisher class for IPC with Python BLE server
class SocketPublisher {
private:
    int serverFd_ = -1;
    int clientFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    SensorDataManager& sensorManager_;

public:
    SocketPublisher(SensorDataManager& manager) : sensorManager_(manager) {}
    
    ~SocketPublisher() {
        stop();
    }
    
    bool start() {
        // Create Unix domain socket
        serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (serverFd_ < 0) {
            std::cerr << "[Socket] Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Remove old socket file if exists
        unlink(SOCKET_PATH);
        
        // Bind socket
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        
        if (bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[Socket] Failed to bind: " << strerror(errno) << std::endl;
            close(serverFd_);
            serverFd_ = -1;
            return false;
        }
        
        // Set socket permissions so Python can connect without sudo
        chmod(SOCKET_PATH, 0666);
        
        // Listen for connections
        if (listen(serverFd_, 1) < 0) {
            std::cerr << "[Socket] Failed to listen: " << strerror(errno) << std::endl;
            close(serverFd_);
            serverFd_ = -1;
            return false;
        }
        
        running_ = true;
        acceptThread_ = std::thread(&SocketPublisher::acceptLoop, this);
        
        std::cout << "[Socket] Server listening on " << SOCKET_PATH << std::endl;
        return true;
    }
    
    void stop() {
        running_ = false;
        
        // Close sockets to unblock accept()
        if (clientFd_ >= 0) {
            close(clientFd_);
            clientFd_ = -1;
        }
        if (serverFd_ >= 0) {
            shutdown(serverFd_, SHUT_RDWR);
            close(serverFd_);
            serverFd_ = -1;
        }
        
        if (acceptThread_.joinable()) {
            acceptThread_.join();
        }
        
        unlink(SOCKET_PATH);
        std::cout << "[Socket] Server stopped" << std::endl;
    }
    
    bool sendData() {
        if (clientFd_ < 0) {
            return false;
        }
        
        // Get binary data from sensor manager
        auto weightBytes = sensorManager_.getWeightDataBinary();
        auto eventsBytes = sensorManager_.getEventsDataBinary();
        
        // Format: "W:<4 hex bytes>,E:<2 hex bytes>\n"
        // Example: "W:0000803F,E:0000\n" (weight as float bytes, events as uint16 bytes)
        char buffer[64];
        snprintf(buffer, sizeof(buffer), 
                 "W:%02X%02X%02X%02X,E:%02X%02X\n",
                 weightBytes[0], weightBytes[1], weightBytes[2], weightBytes[3],
                 eventsBytes[0], eventsBytes[1]);
        
        // Send to Python BLE server
        ssize_t sent = send(clientFd_, buffer, strlen(buffer), MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET) {
                std::cout << "\n[Socket] Python BLE server disconnected" << std::endl;
                close(clientFd_);
                clientFd_ = -1;
            }
            return false;
        }
        
        return true;
    }
    
    bool isClientConnected() const {
        return clientFd_ >= 0;
    }

private:
    void acceptLoop() {
        while (running_) {
            if (clientFd_ >= 0) {
                // Already have a client, wait
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            std::cout << "[Socket] Waiting for Python BLE server to connect..." << std::endl;
            
            // Set timeout on accept using select
            fd_set readfds;
            struct timeval tv;
            FD_ZERO(&readfds);
            FD_SET(serverFd_, &readfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            
            int ret = select(serverFd_ + 1, &readfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(serverFd_, &readfds)) {
                int newClient = accept(serverFd_, nullptr, nullptr);
                if (newClient >= 0) {
                    clientFd_ = newClient;
                    std::cout << "[Socket] Python BLE server connected!" << std::endl;
                }
            }
        }
    }
};

// Main function
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    printBanner();
    printConfig();
    
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create sensor data manager
    SensorDataManager sensorManager;
    
    // Create socket publisher
    SocketPublisher socketPublisher(sensorManager);
    
    // Start socket server
    if (!socketPublisher.start()) {
        std::cerr << "[Main] Failed to start socket server" << std::endl;
        return 1;
    }
    
    // Start sensor data generation
    std::cout << "[Main] Starting sensor data generation..." << std::endl;
    sensorManager.startDataGeneration();
    
    // Print running message
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  C++ SENSOR PUBLISHER RUNNING                         ║" << std::endl;
    std::cout << "║                                                       ║" << std::endl;
    std::cout << "║  Now start the Python BLE server in another terminal: ║" << std::endl;
    std::cout << "║    sudo python3 ble_server.py                         ║" << std::endl;
    std::cout << "║                                                       ║" << std::endl;
    std::cout << "║  Press Ctrl+C to stop                                 ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n";
    
    // Counter for status updates
    int updateCounter = 0;
    
    // Main loop - send data and display status
    while (g_running) {
        // Send data to Python BLE server
        bool sent = socketPublisher.sendData();
        
        // Get current sensor data for display
        WeightData weight = sensorManager.getWeightData();
        EventsData events = sensorManager.getEventsData();
        
        // Display status
        std::cout << "\r[" << std::setw(5) << updateCounter << "] "
                  << "Weight: " << std::fixed << std::setprecision(1) << std::setw(6) << weight.weight << " lb"
                  << " | Events: 0x" << std::hex << std::setw(4) << std::setfill('0') << events.eventBits 
                  << std::dec << std::setfill(' ')
                  << " (" << std::setw(12) << std::left << eventFlagsToString(events.eventBits) << std::right << ")"
                  << " | BLE: " << (sent ? "✓ sending" : "○ waiting")
                  << "    " << std::flush;
        
        updateCounter++;
        
        // Sleep for update interval
        std::this_thread::sleep_for(
            std::chrono::milliseconds(Config::WEIGHT_UPDATE_INTERVAL_MS)
        );
    }
    
    // Cleanup
    std::cout << "\n\n[Main] Shutting down..." << std::endl;
    sensorManager.stopDataGeneration();
    socketPublisher.stop();
    
    std::cout << "[Main] Goodbye!" << std::endl;
    return 0;
}


/*
#include <iostream>
#include <iomanip>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>


#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>


#include "config.hpp"
#include "sensor_data.hpp"

// Global flag for shutdown
std::atomic<bool> g_running(true);

// Global HCI socket for cleanup
int g_hciSocket = -1;

// Signal handler
void signalHandler(int signum) {
    std::cout << "\n[Main] Received signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

// Function to set advertising parameters
bool setAdvertisingParameters(int hciSocket) {
    le_set_advertising_parameters_cp adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    
    // Advertising interval: 100ms to 200ms
    adv_params.min_interval = htobs(0x00A0);  // 100ms (0xA0 * 0.625ms)
    adv_params.max_interval = htobs(0x0140);  // 200ms (0x140 * 0.625ms)
    adv_params.advtype = 0x00;                // Connectable undirected advertising
    adv_params.own_bdaddr_type = 0x00;        // Public device address
    adv_params.direct_bdaddr_type = 0x00;
    memset(adv_params.direct_bdaddr.b, 0, 6);
    adv_params.chan_map = 0x07;               // All channels (37, 38, 39)
    adv_params.filter = 0x00;                 // Allow scan/connect from any
    
    struct hci_request rq;
    uint8_t status;
    
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = OCF_LE_SET_ADVERTISING_PARAMETERS;
    rq.cparam = &adv_params;
    rq.clen = sizeof(adv_params);
    rq.rparam = &status;
    rq.rlen = 1;
    
    if (hci_send_req(hciSocket, &rq, 1000) < 0) {
        std::cerr << "[BLE] Failed to set advertising parameters: " << strerror(errno) << std::endl;
        return false;
    }
    
    return status == 0;
}

// Function to set advertising data
bool setAdvertisingData(int hciSocket) {
    // Advertising data structure
    struct {
        uint8_t length;
        uint8_t data[31];
    } advData;
    
    memset(&advData, 0, sizeof(advData));
    
    int pos = 0;
    
    // Flags (required for BLE)
    advData.data[pos++] = 2;           // Length of this field
    advData.data[pos++] = 0x01;        // Type: Flags
    advData.data[pos++] = 0x06;        // LE General Discoverable + BR/EDR Not Supported
    
    // Complete Local Name
    const char* name = Config::DEVICE_NAME;
    int nameLen = strlen(name);
    advData.data[pos++] = nameLen + 1; // Length of this field
    advData.data[pos++] = 0x09;        // Type: Complete Local Name
    memcpy(&advData.data[pos], name, nameLen);
    pos += nameLen;
    
    advData.length = pos;
    
    // Set advertising data using HCI command
    struct hci_request rq;
    uint8_t status;
    
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = OCF_LE_SET_ADVERTISING_DATA;
    rq.cparam = &advData;
    rq.clen = advData.length + 1;
    rq.rparam = &status;
    rq.rlen = 1;
    
    if (hci_send_req(hciSocket, &rq, 1000) < 0) {
        std::cerr << "[BLE] Failed to set advertising data: " << strerror(errno) << std::endl;
        return false;
    }
    
    return status == 0;
}

// Function to set scan response data (contains service UUID)
bool setScanResponseData(int hciSocket) {
    struct {
        uint8_t length;
        uint8_t data[31];
    } scanRsp;
    
    memset(&scanRsp, 0, sizeof(scanRsp));
    
    int pos = 0;
    
    // Complete list of 128-bit Service UUIDs
    // UUID: 3f09d95b-7f10-4c6a-8f0d-15a74be2b9b5
    // Must be in little-endian byte order for BLE
    scanRsp.data[pos++] = 17;          // Length (1 type byte + 16 UUID bytes)
    scanRsp.data[pos++] = 0x07;        // Type: Complete List of 128-bit Service UUIDs
    
    // UUID in little-endian (reverse byte order)
    // Original: 3f09d95b-7f10-4c6a-8f0d-15a74be2b9b5
    // Reversed: b5-b9-e2-4b-a7-15-0d-8f-6a-4c-10-7f-5b-d9-09-3f
    uint8_t uuid[] = {
        0xb5, 0xb9, 0xe2, 0x4b, 0xa7, 0x15, 0x0d, 0x8f,
        0x6a, 0x4c, 0x10, 0x7f, 0x5b, 0xd9, 0x09, 0x3f
    };
    memcpy(&scanRsp.data[pos], uuid, 16);
    pos += 16;
    
    scanRsp.length = pos;
    
    // Set scan response data
    struct hci_request rq;
    uint8_t status;
    
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = OCF_LE_SET_SCAN_RESPONSE_DATA;
    rq.cparam = &scanRsp;
    rq.clen = scanRsp.length + 1;
    rq.rparam = &status;
    rq.rlen = 1;
    
    if (hci_send_req(hciSocket, &rq, 1000) < 0) {
        std::cerr << "[BLE] Failed to set scan response: " << strerror(errno) << std::endl;
        return false;
    }
    
    return status == 0;
}

// Function to enable/disable advertising
bool enableAdvertising(int hciSocket, bool enable) {
    le_set_advertise_enable_cp advertise_cp;
    memset(&advertise_cp, 0, sizeof(advertise_cp));
    advertise_cp.enable = enable ? 0x01 : 0x00;
    
    struct hci_request rq;
    uint8_t status;
    
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = OCF_LE_SET_ADVERTISE_ENABLE;
    rq.cparam = &advertise_cp;
    rq.clen = sizeof(advertise_cp);
    rq.rparam = &status;
    rq.rlen = 1;
    
    if (hci_send_req(hciSocket, &rq, 1000) < 0) {
        std::cerr << "[BLE] Failed to " << (enable ? "enable" : "disable") 
                  << " advertising: " << strerror(errno) << std::endl;
        return false;
    }
    
    return status == 0;
}

// Print startup banner
void printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║     Raspberry Pi BLE Sensor Server                    ║" << std::endl;
    std::cout << "║     For Rover Weight Monitoring                       ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n";
}

// Print configuration
void printConfig() {
    std::cout << "Configuration:" << std::endl;
    std::cout << "├─ Device Name:     " << Config::DEVICE_NAME << std::endl;
    std::cout << "├─ Service UUID:    " << Config::SERVICE_UUID << std::endl;
    std::cout << "├─ Weight Char:     " << Config::WEIGHT_CHAR_UUID << std::endl;
    std::cout << "├─ Events Char:     " << Config::EVENTS_CHAR_UUID << std::endl;
    std::cout << "├─ Weight Range:    " << Config::WEIGHT_MIN << " - " << Config::WEIGHT_MAX << " lb" << std::endl;
    std::cout << "├─ Overweight At:   " << Config::WEIGHT_THRESHOLD << " lb" << std::endl;
    std::cout << "└─ Update Interval: " << Config::WEIGHT_UPDATE_INTERVAL_MS << " ms" << std::endl;
    std::cout << "\n";
}

// Print event flags in human-readable format
std::string eventFlagsToString(uint16_t flags) {
    std::string result = "";
    
    if (flags == 0) {
        return "NONE";
    }
    
    if (flags & Config::EVENT_OVERWEIGHT) {
        result += "OVERWEIGHT ";
    }
    if (flags & Config::EVENT_OUT_OF_RANGE) {
        result += "OUT_OF_RANGE ";
    }
    if (flags & Config::EVENT_LOW_BATTERY) {
        result += "LOW_BATTERY ";
    }
    if (flags & Config::EVENT_ERROR) {
        result += "ERROR ";
    }
    
    return result;
}

// Cleanup function
void cleanup(int hciSocket) {
    std::cout << "\n[Main] Cleaning up..." << std::endl;
    
    // Disable advertising
    if (hciSocket >= 0) {
        enableAdvertising(hciSocket, false);
        hci_close_dev(hciSocket);
    }
    
    std::cout << "[Main] Cleanup complete." << std::endl;
}

// Main function
int main(int argc, char* argv[]) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;
    
    printBanner();
    printConfig();
    
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create sensor data manager
    SensorDataManager sensorManager;
    
    std::cout << "[Main] Initializing Bluetooth adapter..." << std::endl;
    
    // Get default Bluetooth adapter
    int deviceId = hci_get_route(nullptr);
    if (deviceId < 0) {
        std::cerr << "\n[Main] ERROR: No Bluetooth adapter found!" << std::endl;
        std::cerr << "[Main] Troubleshooting steps:" << std::endl;
        std::cerr << "  1. Check if Bluetooth is enabled: hciconfig -a" << std::endl;
        std::cerr << "  2. Turn on adapter: sudo hciconfig hci0 up" << std::endl;
        std::cerr << "  3. Check Bluetooth service: sudo systemctl status bluetooth" << std::endl;
        return 1;
    }
    
    // Open HCI socket
    int hciSocket = hci_open_dev(deviceId);
    if (hciSocket < 0) {
        std::cerr << "\n[Main] ERROR: Failed to open Bluetooth adapter!" << std::endl;
        std::cerr << "[Main] Error: " << strerror(errno) << std::endl;
        std::cerr << "[Main] Try running with sudo: sudo ./sensor_ble_server" << std::endl;
        return 1;
    }
    
    // Store globally for cleanup
    g_hciSocket = hciSocket;
    
    // Get adapter info
    bdaddr_t adapterAddr;
    hci_devba(deviceId, &adapterAddr);
    char addrStr[18];
    ba2str(&adapterAddr, addrStr);
    std::cout << "[Main] Bluetooth adapter: " << addrStr << " (hci" << deviceId << ")" << std::endl;
    
    // Set device name using HCI
    std::cout << "[Main] Setting device name to: " << Config::DEVICE_NAME << std::endl;
    if (hci_write_local_name(hciSocket, Config::DEVICE_NAME, 2000) < 0) {
        std::cerr << "[Main] Warning: Could not set device name via HCI" << std::endl;
    }
    
    // Configure advertising
    std::cout << "[Main] Configuring BLE advertising..." << std::endl;
    
    // Disable advertising first (in case it's already running)
    enableAdvertising(hciSocket, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Set advertising parameters
    std::cout << "[Main] Setting advertising parameters..." << std::endl;
    if (!setAdvertisingParameters(hciSocket)) {
        std::cerr << "[Main] Warning: Could not set advertising parameters" << std::endl;
    }
    
    // Set advertising data (contains device name)
    std::cout << "[Main] Setting advertising data..." << std::endl;
    if (!setAdvertisingData(hciSocket)) {
        std::cerr << "[Main] Warning: Could not set advertising data" << std::endl;
    }
    
    // Set scan response data (contains service UUID)
    std::cout << "[Main] Setting scan response data..." << std::endl;
    if (!setScanResponseData(hciSocket)) {
        std::cerr << "[Main] Warning: Could not set scan response data" << std::endl;
    }
    
    // Enable advertising
    std::cout << "[Main] Enabling advertising..." << std::endl;
    if (!enableAdvertising(hciSocket, true)) {
        std::cerr << "\n[Main] ERROR: Could not enable advertising!" << std::endl;
        std::cerr << "[Main] Try these commands:" << std::endl;
        std::cerr << "  sudo hciconfig hci0 up" << std::endl;
        std::cerr << "  sudo hciconfig hci0 leadv 3" << std::endl;
        cleanup(hciSocket);
        return 1;
    }
    
    std::cout << "[Main] BLE advertising started successfully!" << std::endl;
    
    // Start dummy data generation
    std::cout << "[Main] Starting sensor data generation..." << std::endl;
    sensorManager.startDataGeneration();
    
    // Print running message
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  SERVER RUNNING - Waiting for connections...          ║" << std::endl;
    std::cout << "║  Your Flutter app should see device: " << std::setw(17) << std::left << Config::DEVICE_NAME << " ║" << std::endl;
    std::cout << "║  Press Ctrl+C to stop                                 ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n";
    
    // Counter for status updates
    int updateCounter = 0;
    
    // Main loop - display data periodically
    while (g_running) {
        // Get current sensor data
        WeightData weight = sensorManager.getWeightData();
        EventsData events = sensorManager.getEventsData();
        
        // Get binary representations (what would be sent via BLE)
        auto weightBinary = sensorManager.getWeightDataBinary();
        auto eventsBinary = sensorManager.getEventsDataBinary();
        
        // Display current values every second
        std::cout << "\r[" << std::setw(5) << updateCounter << "] "
                  << "Weight: " << std::fixed << std::setprecision(1) << std::setw(6) << weight.weight << " lb"
                  << " | Events: 0x" << std::hex << std::setw(4) << std::setfill('0') << events.eventBits << std::dec << std::setfill(' ')
                  << " (" << eventFlagsToString(events.eventBits) << ")"
                  << " | Binary: [";
        
        // Show weight bytes
        for (size_t i = 0; i < weightBinary.size(); i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)weightBinary[i];
            if (i < weightBinary.size() - 1) std::cout << " ";
        }
        std::cout << std::dec << std::setfill(' ') << "]    " << std::flush;
        
        updateCounter++;
        
        // Sleep for update interval
        std::this_thread::sleep_for(std::chrono::milliseconds(Config::WEIGHT_UPDATE_INTERVAL_MS));
    }
    
    // Cleanup
    std::cout << "\n\n[Main] Shutting down..." << std::endl;
    sensorManager.stopDataGeneration();
    cleanup(hciSocket);
    
    std::cout << "[Main] Goodbye!" << std::endl;
    
    return 0;
}
*/