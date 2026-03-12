#include "pch.h"
#include "BatteryItem.h"

// 构造函数
BatteryItem::BatteryItem()
{
}

// 更新单个设备数据
void BatteryItem::Update(const DeviceBattery& dev)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_name = dev.name;
    
    // 对于单个设备更新，用此设备替换选中的设备列表
    m_selectedDevices.clear();
    m_selectedDevices.push_back(dev);
    m_isOnline = dev.isOnline;
    m_itemId = L"Battery_" + dev.id;

    RebuildText();
}

// 更新多个选中的设备
void BatteryItem::UpdateSelectedDevices(const std::vector<DeviceBattery>& devices)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedDevices = devices;
    m_isOnline = !devices.empty();
    m_itemId = L"Battery_Display";
    RebuildText();
}

// 设置设备为离线状态
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

// 使用 ID 初始化项目（在 API 数据可用前）
void BatteryItem::InitWithId(const std::wstring& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_itemId = L"Battery_" + id;
    m_name = id;
    
    m_selectedDevices.clear();
    DeviceBattery fakeDev;
    fakeDev.id = id;
    fakeDev.name = id; // 临时名称就是 ID
    fakeDev.isOnline = false;
    fakeDev.battery = -1;
    m_selectedDevices.push_back(fakeDev);
    
    RebuildText();
}

// 重建显示文本
void BatteryItem::RebuildText()
{
    // 显示索引从1开始（槽位1到4）
    int displayIndex = m_index + 1;

    // 如果没有选中设备或设备无效
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

    // 值文本格式："设备名称 电池图标 电量"
    wchar_t buf[256];
    if (!dev.isOnline || dev.battery < 0)
    {
        swprintf_s(buf, L"%s --", displayName.c_str());
    }
    else if (dev.isCharging)
    {
        swprintf_s(buf, L"%s \u26A1 %d%%", displayName.c_str(), dev.battery); // 闪电图标
    }
    else
    {
        swprintf_s(buf, L"%s \U0001F50B %d%%", displayName.c_str(), dev.battery); // 电池图标
    }
    m_valueText = buf;
}

// 获取项目名称
const wchar_t* BatteryItem::GetItemName() const
{
    static const wchar_t* names[] = { L"设备1：", L"设备2：", L"设备3：", L"设备4：" };
    if (m_index >= 0 && m_index < 4) return names[m_index];
    return L"未知";
}

// 获取项目ID
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

// 获取项目标签文本
const wchar_t* BatteryItem::GetItemLableText() const
{
    return L" ";
}

// 获取项目值文本
const wchar_t* BatteryItem::GetItemValueText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_valueText.empty() ? L"--" : m_valueText.c_str();
}

// 获取项目值示例文本（用于计算列宽）
const wchar_t* BatteryItem::GetItemValueSampleText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 返回中等宽度的字符串以保留列宽度
    // 太长会导致项目之间出现大间隙
    return L"12345678901 \U0001F50B 100%";
}

// 是否绘制资源使用图（电量条）
int BatteryItem::IsDrawResourceUsageGraph() const
{
    return 1;
}

// 获取资源使用图值（电量百分比，范围0.0-1.0）
float BatteryItem::GetResourceUsageGraphValue() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_selectedDevices.empty()) return 0.0f;
    const auto& dev = m_selectedDevices[0];
    if (dev.battery < 0) return 0.0f;
    return dev.battery / 100.0f;
}
