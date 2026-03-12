#include "pch.h"
#include "BatteryItem.h"

BatteryItem::BatteryItem()
{
}

void BatteryItem::Update(const DeviceBattery& dev)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_name = dev.name;
    
    // For single device update, replace the selected devices with just this device
    m_selectedDevices.clear();
    m_selectedDevices.push_back(dev);
    m_isOnline = dev.isOnline;
    m_itemId = L"Battery_" + dev.id;

    RebuildText();
}

void BatteryItem::UpdateSelectedDevices(const std::vector<DeviceBattery>& devices)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedDevices = devices;
    m_isOnline = !devices.empty();
    m_itemId = L"Battery_Display";
    RebuildText();
}

void BatteryItem::SetOffline()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isOnline = false;
    for (auto& dev : m_selectedDevices)
    {
        dev.isOnline = false;
    }
    RebuildText();
}

void BatteryItem::InitWithId(const std::wstring& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_itemId = L"Battery_" + id;
    m_name = id;
    
    m_selectedDevices.clear();
    DeviceBattery fakeDev;
    fakeDev.id = id;
    fakeDev.name = id; // Temporary name is just the ID
    fakeDev.isOnline = false;
    fakeDev.battery = -1;
    m_selectedDevices.push_back(fakeDev);
    
    RebuildText();
}

void BatteryItem::RebuildText()
{
    // 1-based index for display (slot 1 to 4)
    int displayIndex = m_index + 1;

    // If no device is selected or valid
    if (m_selectedDevices.empty())
    {
        m_valueText = L"--";
        return;
    }

    const auto& dev = m_selectedDevices[0];
    
    std::wstring displayName = dev.name;
    if (displayName.length() > 12)
    {
        displayName = displayName.substr(0, 9) + L"...";
    }

    // Value text is "设备名称 电池图标 电量"
    wchar_t buf[256];
    if (!dev.isOnline || dev.battery < 0)
    {
        swprintf_s(buf, L"%s --", displayName.c_str());
    }
    else if (dev.isCharging)
    {
        swprintf_s(buf, L"%s \u26A1 %d%%", displayName.c_str(), dev.battery); // Lightning bolt
    }
    else
    {
        swprintf_s(buf, L"%s \U0001F50B %d%%", displayName.c_str(), dev.battery); // Battery icon
    }
    m_valueText = buf;
}

const wchar_t* BatteryItem::GetItemName() const
{
    static const wchar_t* names[] = { L"设备1：", L"设备2：", L"设备3：", L"设备4：" };
    if (m_index >= 0 && m_index < 4) return names[m_index];
    return L"未知";
}

const wchar_t* BatteryItem::GetItemId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_itemId.empty())
    {
        static wchar_t fallback[32];
        swprintf_s(fallback, L"Battery_%d", m_index);
        return fallback;
    }
    return m_itemId.c_str();
}

const wchar_t* BatteryItem::GetItemLableText() const
{
    return L" ";
}

const wchar_t* BatteryItem::GetItemValueText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_valueText.empty() ? L"--" : m_valueText.c_str();
}

const wchar_t* BatteryItem::GetItemValueSampleText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Return a moderately wide string to reserve column width.
    // Making this too long causes large gaps between items.
    return L"12345678901 \U0001F50B 100%";
}

int BatteryItem::IsDrawResourceUsageGraph() const
{
    return 1;
}

float BatteryItem::GetResourceUsageGraphValue() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_selectedDevices.empty()) return 0.0f;
    const auto& dev = m_selectedDevices[0];
    if (dev.battery < 0) return 0.0f;
    return dev.battery / 100.0f;
}
