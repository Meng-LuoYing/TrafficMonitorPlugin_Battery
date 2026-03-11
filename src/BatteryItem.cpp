#include "pch.h"
#include "BatteryItem.h"

BatteryItem::BatteryItem()
{
}

void BatteryItem::Update(const DeviceBattery& dev)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_name       = dev.name;
    m_battery    = dev.battery;
    m_isCharging = dev.isCharging;
    m_isOnline   = dev.isOnline;

    // Build a stable unique ID from the device hardware ID
    m_itemId = L"Battery_" + dev.id;

    RebuildText();
}

void BatteryItem::SetOffline()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isOnline = false;
    RebuildText();
}

void BatteryItem::RebuildText()
{
    // Label: device name (truncate to 12 chars)
    std::wstring shortName = m_name;
    if (shortName.size() > 12)
        shortName = shortName.substr(0, 11) + L"\u2026"; // ellipsis

    m_labelText = shortName;

    // Value: e.g.  "[bolt] 85%"  or  "[battery] 85%"  or  "--"
    wchar_t buf[64];
    if (!m_isOnline || m_battery < 0)
    {
        swprintf_s(buf, L"--");
    }
    else if (m_isCharging)
    {
        swprintf_s(buf, L"\u26a1 %d%%", m_battery);   // lightning bolt
    }
    else
    {
        swprintf_s(buf, L"\U0001F50B %d%%", m_battery); // battery emoji
    }
    m_valueText = buf;
}

const wchar_t* BatteryItem::GetItemName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_name.empty() ? L"Battery" : m_name.c_str();
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
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_labelText.empty() ? L"Battery" : m_labelText.c_str();
}

const wchar_t* BatteryItem::GetItemValueText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_valueText.empty() ? L"--" : m_valueText.c_str();
}

const wchar_t* BatteryItem::GetItemValueSampleText() const
{
    return L"\u26a1 100%";  // used by TrafficMonitor to measure column width
}

int BatteryItem::IsDrawResourceUsageGraph() const
{
    return 1;
}

float BatteryItem::GetResourceUsageGraphValue() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_battery < 0) return 0.0f;
    return m_battery / 100.0f;
}
