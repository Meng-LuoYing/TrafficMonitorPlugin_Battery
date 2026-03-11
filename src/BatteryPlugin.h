#pragma once
#include "../include/PluginInterface.h"
#include "BatteryItem.h"
#include <vector>
#include <memory>

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

private:
    void InitDevices();
    void FetchAndUpdate();

private:
    static BatteryPlugin m_instance;

    std::vector<std::unique_ptr<BatteryItem>> m_items;
    bool m_initialized = false;
    ITrafficMonitor* m_pApp = nullptr;

    static constexpr const wchar_t* API_HOST = L"127.0.0.1";
    static constexpr int             API_PORT = 18080;
    static constexpr const wchar_t* API_PATH = L"/api/v1/status";
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
