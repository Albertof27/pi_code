#ifndef SENSOR_DATA_HPP
#define SENSOR_DATA_HPP

#include <cstring>
#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

struct WeightData {
    float weight;           // Weight in pounds
    bool stable;            // Reading is stable
    uint64_t timestamp;     // Timestamp in milliseconds
};

struct EventsData {
    uint16_t eventBits;     // Bit flags for events
    uint64_t timestamp;     // Timestamp in milliseconds
};

class SensorDataManager {
public:
    SensorDataManager();
    ~SensorDataManager();
    
    // Start/stop dummy data generation
    void startDataGeneration();
    void stopDataGeneration();
    
    // Get current data
    WeightData getWeightData();
    EventsData getEventsData();
    
    // Get binary formatted data for BLE (Little Endian)
    std::vector<uint8_t> getWeightDataBinary();
    std::vector<uint8_t> getEventsDataBinary();
    // --- ADDED FOR GPS/BEARING ---
    // This returns the calculated angle in binary for your app
    std::vector<uint8_t> getBearingDataBinary();
    /*
    std::vector<uint8_t> getBearingDataBinary(){
        float bearing = calculateBearing(); // Assuming this is your math function
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(), &bearing, 4);
        return bytes;
    }
        */

    // These allow the Python Bluetooth script and GPS module to update locations
    void updatePiLocation(); 
    void updateUserLocation(float phoneLat, float phoneLon);

    void setPiLocation(float lat, float lon);
    
    // Manual control for testing
    void setWeight(float weight);
    void setEventBit(uint16_t bit, bool enabled);
    void clearAllEvents();
    
    // Check if running
    bool isRunning() const { return running_; }
    
private:
    float calculateBearing();
    void generateDummyData();
    uint64_t getCurrentTimestamp();
    void updateEventFlags();
    
    WeightData weightData_;
    EventsData eventsData_;
    // --- ADDED FOR GPS/BEARING ---
    float piLat_, piLon_;      // The Pi's current coordinates
    float userLat_, userLon_;  // The User's (Phone) coordinates
    float bearingToUser_;      // The final calculated angle (0-360)
    
    std::mutex dataMutex_;
    std::atomic<bool> running_;
    std::thread dataThread_;
    
    float weightDirection_;  // For oscillating weight (1.0 or -1.0)
};

#endif // SENSOR_DATA_HPP