#pragma once
#include <string>
#include <vector>

// Information for a single BLE device
struct DeviceBattery
{
    std::wstring id;
    std::wstring name;      // renamedName preferred; falls back to name
    int battery = -1;       // -1 = unknown
    bool isCharging = false;
    bool isOnline = false;
};

// Parse the /api/v1/status JSON response and return all device entries
std::vector<DeviceBattery> ParseBatteryJson(const std::string& json);
