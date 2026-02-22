#ifndef SENSOR_DATA_HPP
#define SENSOR_DATA_HPP

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
    
    // Manual control for testing
    void setWeight(float weight);
    void setEventBit(uint16_t bit, bool enabled);
    void clearAllEvents();
    
    // Check if running
    bool isRunning() const { return running_; }
    
private:
    void generateDummyData();
    uint64_t getCurrentTimestamp();
    void updateEventFlags();
    
    WeightData weightData_;
    EventsData eventsData_;
    
    std::mutex dataMutex_;
    std::atomic<bool> running_;
    std::thread dataThread_;
    
    float weightDirection_;  // For oscillating weight (1.0 or -1.0)
};

#endif // SENSOR_DATA_HPP