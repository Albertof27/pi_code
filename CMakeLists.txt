cmake_minimum_required(VERSION 3.10)
project(sensor_ble_server VERSION 1.0)

## Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Find required packages
#find_package(PkgConfig REQUIRED)

## Find BlueZ (Bluetooth library)
#pkg_check_modules(BLUEZ bluez)

## Find threading library
find_package(Threads REQUIRED)

## Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    #${BLUEZ_INCLUDE_DIRS}
    #/usr/include
)

## Link directories
#link_directories(
 #   ${BLUEZ_LIBRARY_DIRS}
  #  /usr/lib
   # /usr/lib/arm-linux-gnueabihf
#)


## Source files
set(SOURCES
    src/main.cpp
    #src/ble_server.cpp
    src/sensor_data.cpp
)

## Header files (for IDE support)
set(HEADERS
    src/config.hpp
   # src/ble_server.hpp
    src/sensor_data.hpp
)

## Create executable
add_executable(sensor_ble_server ${SOURCES} ${HEADERS})

## Link libraries
target_link_libraries(sensor_ble_server
    #bluetooth
    Threads::Threads
   # ${BLUEZ_LIBRARIES}
)

## Compiler flags
target_compile_options(sensor_ble_server PRIVATE 
    -Wall 
    -Wextra 
    -O2
)

## Installation
install(TARGETS sensor_ble_server DESTINATION bin)

## Print configuration summary
message(STATUS "===========================================")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "BlueZ found: ${BLUEZ_FOUND}")
message(STATUS "BlueZ libraries: ${BLUEZ_LIBRARIES}")
message(STATUS "===========================================")