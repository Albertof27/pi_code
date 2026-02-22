/*
#include "ble_server.hpp"
#include "sensor_data.hpp"
#include "config.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <chrono>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

// Global pointer for signal handling
static BLEServer* g_bleServer = nullptr;

BLEServer::BLEServer(SensorDataManager& sensorManager)
    : dbusConnection_(nullptr)
    , mainLoop_(nullptr)
    , running_(false)
    , clientConnected_(false)
    , weightNotifyEnabled_(false)
    , eventsNotifyEnabled_(false)
    , sensorManager_(sensorManager)
    , adapterPath_("/org/bluez/hci0")
    , serverSocket_(-1) 
{
    g_bleServer = this;
}

BLEServer::~BLEServer() {
    stop();
    g_bleServer = nullptr;
}

bool BLEServer::initialize() {
    std::cout << "[BLEServer] Initializing..." << std::endl;
    
    if (!setupAdapter()) {
        std::cerr << "[BLEServer] Failed to setup adapter" << std::endl;
        return false;
    }
    
    std::cout << "[BLEServer] Initialized successfully" << std::endl;
    return true;
}

bool BLEServer::setupAdapter() {
    std::cout << "[BLEServer] Setting up Bluetooth adapter..." << std::endl;
    
    // Get the default Bluetooth adapter
    int deviceId = hci_get_route(nullptr);
    if (deviceId < 0) {
        std::cerr << "[BLEServer] No Bluetooth adapter found" << std::endl;
        return false;
    }
    
    // Open HCI socket
    int hciSocket = hci_open_dev(deviceId);
    if (hciSocket < 0) {
        std::cerr << "[BLEServer] Failed to open HCI device" << std::endl;
        return false;
    }
    
    // Get adapter address for logging
    bdaddr_t adapterAddr;
    hci_devba(deviceId, &adapterAddr);
    char addrStr[18];
    ba2str(&adapterAddr, addrStr);
    std::cout << "[BLEServer] Using adapter: " << addrStr << std::endl;
    
    hci_close_dev(hciSocket);
    
    return true;
}

bool BLEServer::registerApplication() {
    std::cout << "[BLEServer] Application registered" << std::endl;
    return true;
}

bool BLEServer::startAdvertising() {
    std::cout << "[BLEServer] Starting advertising..." << std::endl;
    system("sudo hciconfig hci0 leadv 3 2>/dev/null");
    std::cout << "[BLEServer] Advertising started" << std::endl;
    return true;
}

void BLEServer::stopAdvertising() {
    std::cout << "[BLEServer] Stopping advertising..." << std::endl;
    system("sudo hciconfig hci0 noleadv 2>/dev/null");
    std::cout << "[BLEServer] Advertising stopped" << std::endl;
}

bool BLEServer::start() {
    if (running_) {
        std::cout << "[BLEServer] Already running" << std::endl;
        return true;
    }
    
    std::cout << "[BLEServer] Starting server..." << std::endl;
    
    running_ = true;
    
    // Start notification thread
    notifyThread_ = std::thread(&BLEServer::notificationLoop, this);
    
    // Start advertising
    if (!startAdvertising()) {
        std::cerr << "[BLEServer] Failed to start advertising" << std::endl;
        running_ = false;
        return false;
    }
    
    std::cout << "[BLEServer] Server started" << std::endl;
    return true;
}

void BLEServer::stop() {
    if (!running_) {
        return;
    }
    
    std::cout << "[BLEServer] Stopping server..." << std::endl;
    
    running_ = false;
    clientConnected_ = false;
    
    // Stop advertising
    stopAdvertising();
    
    // Wait for notification thread to finish
    if (notifyThread_.joinable()) {
        notifyThread_.join();
    }
    
    // Close server socket if open
    if (serverSocket_ >= 0) {
        close(serverSocket_);
        serverSocket_ = -1;
    }
    
    std::cout << "[BLEServer] Server stopped" << std::endl;
}

void BLEServer::notificationLoop() {
    std::cout << "[BLEServer] Notification loop started" << std::endl;
    
    while (running_) {
        if (clientConnected_) {
            if (weightNotifyEnabled_) {
                notifyWeight();
            }
            if (eventsNotifyEnabled_) {
                notifyEvents();
            }
        }
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(Config::WEIGHT_UPDATE_INTERVAL_MS)
        );
    }
    
    std::cout << "[BLEServer] Notification loop ended" << std::endl;
}

void BLEServer::notifyWeight() {
    if (!clientConnected_ || !weightNotifyEnabled_) {
        return;
    }
    
    auto data = sensorManager_.getWeightDataBinary();
    std::cout << "[BLEServer] Would notify weight: " << data.size() << " bytes" << std::endl;
}

void BLEServer::notifyEvents() {
    if (!clientConnected_ || !eventsNotifyEnabled_) {
        return;
    }
    
    auto data = sensorManager_.getEventsDataBinary();
    std::cout << "[BLEServer] Would notify events: " << data.size() << " bytes" << std::endl;
}

void BLEServer::run() {
    std::cout << "[BLEServer] Running main loop..." << std::endl;
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[BLEServer] Main loop ended" << std::endl;
}
    */