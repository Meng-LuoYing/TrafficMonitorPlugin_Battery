#pragma once
#include "../include/PluginInterface.h"
#include "BatteryItem.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <set>

class BatteryPlugin : public ITMPlugin
{
private:
    BatteryPlugin();

public:
    static BatteryPlugin& Instance();

    // ITMPlugin interface
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;

    // Public methods for device selection
    std::vector<DeviceBattery> GetAvailableDevices() const;
    void SetDeviceSelection(const std::wstring& deviceId, bool selected);
    bool IsDeviceSelected(const std::wstring& deviceId) const;
    void RefreshDevicesNow(); // Force an immediate API fetch and device list update

private:
    void InitDevices();
    void FetchAndUpdate(bool syncDevices);
    void LoadConfig();
    void SaveConfig();
    void UpdateConfigDir(const wchar_t* dir);
    std::wstring GetConfigPath() const;
    void RebuildItems(); // Rebuild m_items vector based strictly on m_selectedDevices

private:
    static BatteryPlugin m_instance;

    std::vector<std::unique_ptr<BatteryItem>> m_items;
    std::unique_ptr<BatteryItem> m_displayItem;
    std::set<std::wstring> m_selectedDevices;
    std::vector<DeviceBattery> m_availableDevices;
    bool m_initialized = false;
    bool m_autoSelectFirstDevices = false;
    ITrafficMonitor* m_pApp = nullptr;
    int m_apiPort = 18080;
    std::wstring m_apiToken;
    int m_authFailCount = 0;
    bool m_pluginDisabled = false;
    int m_deviceSyncIntervalMs = 5000;
    int m_batteryRefreshIntervalMs = 2000;
    unsigned long long m_lastDeviceSyncTick = 0;
    unsigned long long m_lastBatteryRefreshTick = 0;
    std::wstring m_configDir;
    mutable std::mutex m_mutex;

    static constexpr const wchar_t* API_HOST = L"127.0.0.1";
    static constexpr const wchar_t* API_PATH = L"/api/v1/status";
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
