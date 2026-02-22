/*
#ifndef BLE_SERVER_HPP
#define BLE_SERVER_HPP

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

// Forward declaration
class SensorDataManager;

class BLEServer {
public:
    BLEServer(SensorDataManager& sensorManager);
    ~BLEServer();
    
    // Initialize the BLE server
    bool initialize();
    
    // Start advertising and accepting connections
    bool start();
    
    // Stop the server
    void stop();
    
    // Check if a client is connected
    bool isClientConnected() const { return clientConnected_; }
    
    // Send notifications to connected client
    void notifyWeight();
    void notifyEvents();
    
    // Run the main loop (blocking)
    void run();
    
private:
    // Internal methods
    bool setupAdapter();
    bool registerApplication();
    bool startAdvertising();
    void stopAdvertising();
    
    // Notification thread
    void notificationLoop();
    
    // D-Bus related handles (using void* to avoid header dependencies)
    void* dbusConnection_;
    void* mainLoop_;
    
    // State
    std::atomic<bool> running_;
    std::atomic<bool> clientConnected_;
    std::atomic<bool> weightNotifyEnabled_;
    std::atomic<bool> eventsNotifyEnabled_;
    
    // Notification thread
    std::thread notifyThread_;
    std::mutex notifyMutex_;
    
    // Reference to sensor data
    SensorDataManager& sensorManager_;
    
    // Adapter path
    std::string adapterPath_;
    
    // Socket for GATT server
    int serverSocket_;
};

#endif // BLE_SERVER_HPP
*/