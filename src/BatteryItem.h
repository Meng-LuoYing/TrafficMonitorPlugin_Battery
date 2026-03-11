#pragma once
#include "../include/PluginInterface.h"
#include "JsonParser.h"
#include <string>
#include <mutex>

// One display item per BLE device, implements IPluginItem
class BatteryItem : public IPluginItem
{
public:
    BatteryItem();

    // Called by BatteryPlugin to push fresh device data
    void Update(const DeviceBattery& dev);
    // Mark device as offline (no longer in API response)
    void SetOffline();
    // Set slot index (used to generate a stable unique ID)
    void SetIndex(int idx) { m_index = idx; }

    // IPluginItem interface
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual int IsDrawResourceUsageGraph() const override;
    virtual float GetResourceUsageGraphValue() const override;

private:
    mutable std::wstring m_name;
    mutable std::wstring m_labelText;
    mutable std::wstring m_valueText;
    mutable std::wstring m_itemId;
    int  m_battery   = -1;
    bool m_isCharging = false;
    bool m_isOnline   = false;
    int  m_index      = 0;

    mutable std::mutex m_mutex;

    void RebuildText();
};
