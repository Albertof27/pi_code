#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace Config {
    
    // BLE UUIDs - MUST MATCH YOUR FLUTTER APP!
   
    
    // Service UUID
    constexpr const char* SERVICE_UUID = "3f09d95b-7f10-4c6a-8f0d-15a74be2b9b5";
    
    // Characteristic UUIDs
    constexpr const char* WEIGHT_CHAR_UUID = "a18f1f42-1f7d-4f62-9b9c-57e76a4c3140";
    constexpr const char* EVENTS_CHAR_UUID = "b3a1f6d4-37db-4e7c-a7ac-b3e74c3f8e6a";
    
    
    // Device Information
    
    constexpr const char* DEVICE_NAME = "Rover-01";
    
    
    // Timing Configuration
  
    constexpr int WEIGHT_UPDATE_INTERVAL_MS = 1000;  // Send weight every 1 second
    constexpr int EVENTS_UPDATE_INTERVAL_MS = 2000;  // Send events every 2 seconds
    
   
    // Dummy Weight Data Configuration
    
    constexpr float WEIGHT_MIN = 0.0f;       // Minimum weight (lb)
    constexpr float WEIGHT_MAX = 25.0f;     // Maximum weight (lb)
    constexpr float WEIGHT_INCREMENT = 2.0f; // Weight change per update
    constexpr float WEIGHT_THRESHOLD = 20.0f; // Overweight threshold (lb)
    
   
    // Events Bit Flags
   
    constexpr uint16_t EVENT_NONE = 0x0000;
    constexpr uint16_t EVENT_OVERWEIGHT = 0x0001;    // Bit 0: Overweight
    constexpr uint16_t EVENT_OUT_OF_RANGE = 0x0002;  // Bit 1: Out of range
    constexpr uint16_t EVENT_LOW_BATTERY = 0x0004;   // Bit 2: Low battery (future)
    constexpr uint16_t EVENT_ERROR = 0x0008;         // Bit 3: Error (future)
}

#endif // CONFIG_HPP