#include "sensor_data.hpp"
#include "config.hpp"

#include <chrono>
#include <cstring>
#include <cmath>
#include <random>
#include <iostream>

SensorDataManager::SensorDataManager()
    : running_(false), weightDirection_(1.0f) {
    
    // Initialize with default values
    weightData_ = {
        0.0f,   // weight
        true,   // stable
        0       // timestamp
    };
    
    eventsData_ = {
        Config::EVENT_NONE,  // no events
        0                    // timestamp
    };
}

SensorDataManager::~SensorDataManager() {
    stopDataGeneration();
}

void SensorDataManager::startDataGeneration() {
    if (running_) {
        return;
    }
    
    running_ = true;
    dataThread_ = std::thread(&SensorDataManager::generateDummyData, this);
    std::cout << "[SensorData] Started dummy data generation" << std::endl;
}

void SensorDataManager::stopDataGeneration() {
    running_ = false;
    if (dataThread_.joinable()) {
        dataThread_.join();
    }
    std::cout << "[SensorData] Stopped dummy data generation" << std::endl;
}

uint64_t SensorDataManager::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void SensorDataManager::updateEventFlags() {
    // Update event flags based on current weight
    uint16_t newEvents = Config::EVENT_NONE;
    
    // Check if overweight
    if (weightData_.weight > Config::WEIGHT_THRESHOLD) {
        newEvents |= Config::EVENT_OVERWEIGHT;
    }
    
    // Out of range would be determined by actual distance measurement
    // For dummy data, we'll toggle it occasionally
    // (In real implementation, this would come from actual sensor)
    
    eventsData_.eventBits = newEvents;
    eventsData_.timestamp = getCurrentTimestamp();
}

void SensorDataManager::generateDummyData() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    
    float currentWeight = Config::WEIGHT_MIN;
    
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            
            // Update weight (oscillate between min and max)
            currentWeight += Config::WEIGHT_INCREMENT * weightDirection_;
            
            // Reverse direction at limits
            if (currentWeight >= Config::WEIGHT_MAX) {
                weightDirection_ = -1.0f;
                currentWeight = Config::WEIGHT_MAX;
            } else if (currentWeight <= Config::WEIGHT_MIN) {
                weightDirection_ = 1.0f;
                currentWeight = Config::WEIGHT_MIN;
            }
            
            // Add some noise for realism
            weightData_.weight = currentWeight + noise(gen);
            
            // Clamp to valid range
            if (weightData_.weight < 0) weightData_.weight = 0;
            
            // Stability: stable if noise is small
            weightData_.stable = (std::abs(noise(gen)) < 0.3f);
            weightData_.timestamp = getCurrentTimestamp();
            
            // Update event flags based on weight
            updateEventFlags();
        }
        
        // Print current values for debugging
        std::cout << "[SensorData] Weight: " << weightData_.weight 
                  << " lb, Events: 0x" << std::hex << eventsData_.eventBits 
                  << std::dec << std::endl;
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(Config::WEIGHT_UPDATE_INTERVAL_MS)
        );
    }
}

WeightData SensorDataManager::getWeightData() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return weightData_;
}

EventsData SensorDataManager::getEventsData() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return eventsData_;
}

std::vector<uint8_t> SensorDataManager::getWeightDataBinary() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    // Pack as Float32, Little Endian (4 bytes) - matches your Flutter app!
    std::vector<uint8_t> data(4);
    
    // Copy float bytes directly (assumes little-endian system like Raspberry Pi)
    std::memcpy(data.data(), &weightData_.weight, sizeof(float));
    
    return data;
}

std::vector<uint8_t> SensorDataManager::getEventsDataBinary() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    // Pack as Uint16, Little Endian (2 bytes) - matches your Flutter app!
    std::vector<uint8_t> data(2);
    
    // Little endian: low byte first
    data[0] = static_cast<uint8_t>(eventsData_.eventBits & 0xFF);
    data[1] = static_cast<uint8_t>((eventsData_.eventBits >> 8) & 0xFF);
    
    return data;
}

void SensorDataManager::setWeight(float weight) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    weightData_.weight = weight;
    weightData_.timestamp = getCurrentTimestamp();
    updateEventFlags();
}

void SensorDataManager::setEventBit(uint16_t bit, bool enabled) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (enabled) {
        eventsData_.eventBits |= bit;
    } else {
        eventsData_.eventBits &= ~bit;
    }
    eventsData_.timestamp = getCurrentTimestamp();
}

void SensorDataManager::clearAllEvents() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    eventsData_.eventBits = Config::EVENT_NONE;
    eventsData_.timestamp = getCurrentTimestamp();
}