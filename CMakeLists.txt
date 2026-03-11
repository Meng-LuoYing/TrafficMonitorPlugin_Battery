cmake_minimum_required(VERSION 3.10)
project(TrafficMonitorBatteryPlugin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/BatteryPlugin.cpp
    include/BatteryPlugin.h
    include/PluginInterface.h
    src/dllmain.cpp
)

# Create shared library
add_library(TrafficMonitorBatteryPlugin SHARED ${SOURCES})

# Link libraries
# MinGW automatically links standard libraries, but we need winhttp
target_link_libraries(TrafficMonitorBatteryPlugin PRIVATE winhttp)

# Ensure static linking for standard libraries to avoid dependency issues on other machines
if(MINGW)
    target_link_options(TrafficMonitorBatteryPlugin PRIVATE -static-libgcc -static-libstdc++ -Wl,--add-stdcall-alias)
endif()
