#include "sensor_data.hpp"
#include "config.hpp"

#include <chrono>
#include <cstring>
#include <cmath>
#include <iostream>
#include <pigpio.h> // REQUIRED FOR HARDWARE GPIO

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- HARDWARE CONFIGURATION ---
const int DAT_PIN = 5; // GPIO 5 (Pin 29)
const int CLK_PIN = 6; // GPIO 6 (Pin 31)

// YOU MUST CALIBRATE THIS VALUE! 
// This converts the raw sensor number into actual Pounds (lbs) or Kilograms (kg).
// Start with 1.0, put a known weight on the scale, and do: SCALE = (Raw_Value / Known_Weight)
const float SCALE_FACTOR = 10000.0f; 

// Helper function to read the HX711 via bit-banging
static int32_t readHX711Raw() {
    // Wait for the sensor to be ready (DAT goes low)
    int timeout = 10000;
    while(gpioRead(DAT_PIN) == 1 && timeout > 0) {
        gpioDelay(10);
        timeout--;
    }
    if (timeout == 0) return 0; // Sensor timeout error

    int32_t count = 0;
    
    // Pulse the clock pin 24 times to read the 24-bit data
    for(int i = 0; i < 24; i++) {
        gpioWrite(CLK_PIN, 1);
        gpioDelay(2); // 2 microsecond pulse
        count = count << 1;
        gpioWrite(CLK_PIN, 0);
        gpioDelay(2);
        if(gpioRead(DAT_PIN)) count++;
    }
    
    // 25th pulse sets the gain to 128 for the next reading
    gpioWrite(CLK_PIN, 1);
    gpioDelay(2);
    gpioWrite(CLK_PIN, 0);
    gpioDelay(2);

    // Convert 24-bit 2's complement number to 32-bit signed integer
    if(count & 0x800000) {
        count |= 0xFF000000;
    }
    return count;
}


SensorDataManager::SensorDataManager()
    : running_(false), weightDirection_(1.0f) {
    
    // Initialize with default values
    weightData_ = { 0.0f, true, 0 };
    eventsData_ = { Config::EVENT_NONE, 0 };
    piLat_ = 30.62413f; 
    piLon_ = -96.3437556f;
    userLat_ = 0.0f;
    userLon_ = 0.0f;

    // --- INITIALIZE HARDWARE ---
    if (gpioInitialise() < 0) {
        std::cerr << "[SensorData] ERROR: Failed to initialize pigpio! Are you running as sudo?" << std::endl;
    } else {
        gpioSetMode(CLK_PIN, PI_OUTPUT);
        gpioSetMode(DAT_PIN, PI_INPUT);
        gpioWrite(CLK_PIN, 0); // Start clock low
        std::cout << "[SensorData] Hardware GPIO initialized successfully." << std::endl;
    }
}

SensorDataManager::~SensorDataManager() {
    stopDataGeneration();
    gpioTerminate(); // Clean up hardware resources
}

void SensorDataManager::startDataGeneration() {
    if (running_) return;
    
    running_ = true;
    // We are keeping the function name generateDummyData so you don't have to change 
    // your .hpp header file, but it is now reading REAL data.
    dataThread_ = std::thread(&SensorDataManager::generateDummyData, this);
    std::cout << "[SensorData] Started hardware sensor polling" << std::endl;
}

void SensorDataManager::stopDataGeneration() {
    running_ = false;
    if (dataThread_.joinable()) {
        dataThread_.join();
    }
    std::cout << "[SensorData] Stopped hardware sensor polling" << std::endl;
}

uint64_t SensorDataManager::getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void SensorDataManager::updateEventFlags() {
    uint16_t newEvents = Config::EVENT_NONE;
    if (weightData_.weight > Config::WEIGHT_THRESHOLD) {
        newEvents |= Config::EVENT_OVERWEIGHT;
    }
    eventsData_.eventBits = newEvents;
    eventsData_.timestamp = getCurrentTimestamp();
}

void SensorDataManager::generateDummyData() {
    // TARE / ZEROING LOGIC
    std::cout << "[SensorData] Taring sensor... Please ensure scale is empty." << std::endl;
    long zeroOffset = 0;
    long sum = 0;
    int samples = 10;
    
    for(int i = 0; i < samples; i++) {
        sum += readHX711Raw();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    zeroOffset = sum / samples;
    std::cout << "[SensorData] Zero Offset calculated: " << zeroOffset << std::endl;

    // MAIN POLLING LOOP
    while (running_) {
        // Read actual hardware
        long raw_val = readHX711Raw();
        
        // Calculate weight: subtract the empty offset, then divide by scale factor
        float currentWeight = static_cast<float>(raw_val - zeroOffset) / SCALE_FACTOR;
        
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            
            // Clamp to valid range (no negative weights)
            if (currentWeight < 0.1f) currentWeight = 0.0f;
            
            weightData_.weight = currentWeight;
            weightData_.stable = true; // You can add variance math later to check stability
            weightData_.timestamp = getCurrentTimestamp();
            
            updateEventFlags();
        }
        
        std::cout << "[SensorData] Raw: " << raw_val 
                  << " | Weight: " << weightData_.weight << " lbs" << std::endl;
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(Config::WEIGHT_UPDATE_INTERVAL_MS)
        );
    }
}

// ... Keep the rest of your methods exactly the same! ...
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
    std::vector<uint8_t> data(4);
    std::memcpy(data.data(), &weightData_.weight, sizeof(float));
    return data;
}

std::vector<uint8_t> SensorDataManager::getEventsDataBinary() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<uint8_t> data(2);
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
    if (enabled) eventsData_.eventBits |= bit;
    else eventsData_.eventBits &= ~bit;
    eventsData_.timestamp = getCurrentTimestamp();
}

void SensorDataManager::clearAllEvents() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    eventsData_.eventBits = Config::EVENT_NONE;
    eventsData_.timestamp = getCurrentTimestamp();
}

void SensorDataManager::updatePiLocation() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    piLat_ = 51.5074f; 
    piLon_ = -0.1278f;
}

void SensorDataManager::updateUserLocation(float phoneLat, float phoneLon) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    userLat_ = phoneLat;
    userLon_ = phoneLon;

    double lat1 = piLat_ * M_PI / 180.0;
    double lat2 = userLat_ * M_PI / 180.0;
    double dLon = (userLon_ - piLon_) * M_PI / 180.0;

    double y = std::sin(dLon) * std::cos(lat2);
    double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    double bearing = std::atan2(y, x);

    bearingToUser_ = static_cast<float>(std::fmod((bearing * 180.0 / M_PI) + 360.0, 360.0));
    std::cout << "\n[Algorithm] Angle to User: " << bearingToUser_ << " degrees" << std::endl;
}

std::vector<uint8_t> SensorDataManager::getBearingDataBinary() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<uint8_t> data(4);
    std::memcpy(data.data(), &bearingToUser_, sizeof(float));
    return data;
}






// #include "sensor_data.hpp"
// #include "config.hpp"

// #include <chrono>
// #include <cstring>
// #include <cmath>
// #include <random>
// #include <iostream>
// #include <pigpio.h>
// #ifndef M_PI
// #define M_PI 3.14159265358979323846
// #endif

// const int DAT_PIN = 5; // GPIO 5 (Pin 29)
// const int CLK_PIN = 6; // GPIO 6 (Pin 31)

// // YOU MUST CALIBRATE THIS VALUE! 
// // This converts the raw sensor number into actual Pounds (lbs) or Kilograms (kg).
// // Start with 1.0, put a known weight on the scale, and do: SCALE = (Raw_Value / Known_Weight)
// const float SCALE_FACTOR = 10000.0f;





// static int32_t readHX711Raw() {
//     // Wait for the sensor to be ready (DAT goes low)
//     int timeout = 10000;
//     while(gpioRead(DAT_PIN) == 1 && timeout > 0) {
//         gpioDelay(10);
//         timeout--;
//     }
//     if (timeout == 0) return 0; // Sensor timeout error

//     int32_t count = 0;
    
//     // Pulse the clock pin 24 times to read the 24-bit data
//     for(int i = 0; i < 24; i++) {
//         gpioWrite(CLK_PIN, 1);
//         gpioDelay(2); // 2 microsecond pulse
//         count = count << 1;
//         gpioWrite(CLK_PIN, 0);
//         gpioDelay(2);
//         if(gpioRead(DAT_PIN)) count++;
//     }
    
//     // 25th pulse sets the gain to 128 for the next reading
//     gpioWrite(CLK_PIN, 1);
//     gpioDelay(2);
//     gpioWrite(CLK_PIN, 0);
//     gpioDelay(2);

//     // Convert 24-bit 2's complement number to 32-bit signed integer
//     if(count & 0x800000) {
//         count |= 0xFF000000;
//     }
//     return count;
// }






// SensorDataManager::SensorDataManager()
//     : running_(false), weightDirection_(1.0f) {
    
//     // Initialize with default values
//     weightData_ = { 0.0f, true, 0 };
//     eventsData_ = { Config::EVENT_NONE, 0 };
//     piLat_ = 30.62413f; 
//     piLon_ = -96.3437556f;
//     userLat_ = 0.0f;
//     userLon_ = 0.0f;



//     // --- INITIALIZE HARDWARE ---
//     if (gpioInitialise() < 0) {
//         std::cerr << "[SensorData] ERROR: Failed to initialize pigpio! Are you running as sudo?" << std::endl;
//     } else {
//         gpioSetMode(CLK_PIN, PI_OUTPUT);
//         gpioSetMode(DAT_PIN, PI_INPUT);
//         gpioWrite(CLK_PIN, 0); // Start clock low
//         std::cout << "[SensorData] Hardware GPIO initialized successfully." << std::endl;
//     }
// }

// SensorDataManager::SensorDataManager()
//     : running_(false), weightDirection_(1.0f) {
    
//     // Initialize with default values
//     weightData_ = {
//         0.0f,   // weight
//         true,   // stable
//         0       // timestamp
//     };
    
//     eventsData_ = {
//         Config::EVENT_NONE,  // no events
//         0                    // timestamp
//     };
//     piLat_ = 30.62413f; 
//     piLon_ = -96.3437556f;
//     userLat_ = 0.0f;
//     userLon_ = 0.0f;
// }

// SensorDataManager::~SensorDataManager() {
//     stopDataGeneration();
// }

// void SensorDataManager::startDataGeneration() {
//     if (running_) {
//         return;
//     }
    
//     running_ = true;
//     dataThread_ = std::thread(&SensorDataManager::generateDummyData, this);
//     std::cout << "[SensorData] Started dummy data generation" << std::endl;
// }

// void SensorDataManager::stopDataGeneration() {
//     running_ = false;
//     if (dataThread_.joinable()) {
//         dataThread_.join();
//     }
//     std::cout << "[SensorData] Stopped dummy data generation" << std::endl;
// }

// uint64_t SensorDataManager::getCurrentTimestamp() {
//     return std::chrono::duration_cast<std::chrono::milliseconds>(
//         std::chrono::system_clock::now().time_since_epoch()
//     ).count();
// }

// void SensorDataManager::updateEventFlags() {
//     // Update event flags based on current weight
//     uint16_t newEvents = Config::EVENT_NONE;
    
//     // Check if overweight
//     if (weightData_.weight > Config::WEIGHT_THRESHOLD) {
//         newEvents |= Config::EVENT_OVERWEIGHT;
//     }
    
//     // Out of range would be determined by actual distance measurement
//     // For dummy data, we'll toggle it occasionally
//     // (In real implementation, this would come from actual sensor)
    
//     eventsData_.eventBits = newEvents;
//     eventsData_.timestamp = getCurrentTimestamp();
// }

// void SensorDataManager::generateDummyData() {
//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    
//     float currentWeight = Config::WEIGHT_MIN;
    
//     while (running_) {
//         {
//             std::lock_guard<std::mutex> lock(dataMutex_);
            
//             // Update weight (oscillate between min and max)
//             currentWeight += Config::WEIGHT_INCREMENT * weightDirection_;
            
//             // Reverse direction at limits
//             if (currentWeight >= Config::WEIGHT_MAX) {
//                 weightDirection_ = -1.0f;
//                 currentWeight = Config::WEIGHT_MAX;
//             } else if (currentWeight <= Config::WEIGHT_MIN) {
//                 weightDirection_ = 1.0f;
//                 currentWeight = Config::WEIGHT_MIN;
//             }
            
//             // Add some noise for realism
//             weightData_.weight = currentWeight + noise(gen);
            
//             // Clamp to valid range
//             if (weightData_.weight < 0) weightData_.weight = 0;
            
//             // Stability: stable if noise is small
//             weightData_.stable = (std::abs(noise(gen)) < 0.3f);
//             weightData_.timestamp = getCurrentTimestamp();
            
//             // Update event flags based on weight
//             updateEventFlags();
//         }
        
//         // Print current values for debugging
//         std::cout << "[SensorData] Weight: " << weightData_.weight 
//                   << " lb, Events: 0x" << std::hex << eventsData_.eventBits 
//                   << std::dec << std::endl;
        
//         std::this_thread::sleep_for(
//             std::chrono::milliseconds(Config::WEIGHT_UPDATE_INTERVAL_MS)
//         );
//     }
// }

// WeightData SensorDataManager::getWeightData() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
//     return weightData_;
// }

// EventsData SensorDataManager::getEventsData() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
//     return eventsData_;
// }

// std::vector<uint8_t> SensorDataManager::getWeightDataBinary() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
    
//     // Pack as Float32, Little Endian (4 bytes) - matches your Flutter app!
//     std::vector<uint8_t> data(4);
    
//     // Copy float bytes directly (assumes little-endian system like Raspberry Pi)
//     std::memcpy(data.data(), &weightData_.weight, sizeof(float));
    
//     return data;
// }

// std::vector<uint8_t> SensorDataManager::getEventsDataBinary() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
    
//     // Pack as Uint16, Little Endian (2 bytes) - matches your Flutter app!
//     std::vector<uint8_t> data(2);
    
//     // Little endian: low byte first
//     data[0] = static_cast<uint8_t>(eventsData_.eventBits & 0xFF);
//     data[1] = static_cast<uint8_t>((eventsData_.eventBits >> 8) & 0xFF);
    
//     return data;
// }

// void SensorDataManager::setWeight(float weight) {
//     std::lock_guard<std::mutex> lock(dataMutex_);
//     weightData_.weight = weight;
//     weightData_.timestamp = getCurrentTimestamp();
//     updateEventFlags();
// }

// void SensorDataManager::setEventBit(uint16_t bit, bool enabled) {
//     std::lock_guard<std::mutex> lock(dataMutex_);
//     if (enabled) {
//         eventsData_.eventBits |= bit;
//     } else {
//         eventsData_.eventBits &= ~bit;
//     }
//     eventsData_.timestamp = getCurrentTimestamp();
// }

// void SensorDataManager::clearAllEvents() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
//     eventsData_.eventBits = Config::EVENT_NONE;
//     eventsData_.timestamp = getCurrentTimestamp();
// }

// // Update this function with your manual coordinates until the GPS arrives
// void SensorDataManager::updatePiLocation() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
    
//     // EDIT THESE VALUES MANUALLY FOR NOW
//     piLat_ = 51.5074f; 
//     piLon_ = -0.1278f;
// }

// // This function will be called whenever the Bluetooth script receives new phone data
// void SensorDataManager::updateUserLocation(float phoneLat, float phoneLon) {
//     std::lock_guard<std::mutex> lock(dataMutex_);
    
//     userLat_ = phoneLat;
//     userLon_ = phoneLon;

//     // Calculate the bearing (angle) from Pi to User
//     // Formula: atan2(sin(Δλ) * cos(φ2), cos(φ1) * sin(φ2) - sin(φ1) * cos(φ2) * cos(Δλ))
    
//     double lat1 = piLat_ * M_PI / 180.0;
//     double lat2 = userLat_ * M_PI / 180.0;
//     double dLon = (userLon_ - piLon_) * M_PI / 180.0;

//     double y = std::sin(dLon) * std::cos(lat2);
//     double x = std::cos(lat1) * std::sin(lat2) -
//                std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    
//     double bearing = std::atan2(y, x);

//     bearingToUser_ = static_cast<float>(std::fmod((bearing * 180.0 / M_PI) + 360.0, 360.0));
    
//     // Convert radians to degrees and normalize to 0-360
//     //float angle = static_cast<float>(std::fmod((bearing * 180.0 / M_PI) + 360.0, 360.0));
    
//     std::cout << "\n[Algorithm] Angle to User: " << bearingToUser_ << " degrees" << std::endl;
    
//     // You can now use 'angle' to point a motor or log it
// }

// std::vector<uint8_t> SensorDataManager::getBearingDataBinary() {
//     std::lock_guard<std::mutex> lock(dataMutex_);
    
//     // Calculate current bearing (or use a stored member variable)
//     // We'll pack it as a 4-byte float for your Flutter/Mobile app
//     std::vector<uint8_t> data(4);
    
//     // Logic: you might want to store the result of the calculation 
//     // in a member variable like 'lastCalculatedBearing_'
//     //float bearing = /* the result from your updateUserLocation logic */;
    
//     std::memcpy(data.data(), &bearingToUser_, sizeof(float));
//     return data;
// }