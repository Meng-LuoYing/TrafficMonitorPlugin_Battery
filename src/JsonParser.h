#pragma once
#include <string>
#include <vector>

// 单个 BLE 设备的信息结构
struct DeviceBattery
{
    std::wstring id;           // 设备ID
    std::wstring name;         // 设备名称（优先使用 renamedName，回退到 name）
    int battery = -1;          // 电量值，-1 表示未知
    bool isCharging = false;  // 是否正在充电
    bool isOnline = false;     // 是否在线
};

// 解析 /api/v1/status JSON 响应并返回所有设备条目
std::vector<DeviceBattery> ParseBatteryJson(const std::string& json);
