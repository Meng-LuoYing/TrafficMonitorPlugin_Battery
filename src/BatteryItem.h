#pragma once
#include "../include/PluginInterface.h"
#include "JsonParser.h"
#include <string>
#include <mutex>

// 电池显示项类：每个 BLE 设备对应一个显示项，实现 IPluginItem 接口
class BatteryItem : public IPluginItem
{
public:
    BatteryItem();

    // 由 BatteryPlugin 调用，用于推送最新的设备数据
    void Update(const DeviceBattery& dev);
    // 更新多个选中的设备
    void UpdateSelectedDevices(const std::vector<DeviceBattery>& devices);
    // 标记设备为离线状态（不再出现在 API 响应中）
    void SetOffline();
    // 在 API 数据可用前，使用配置中的 ID 预初始化项目
    void InitWithId(const std::wstring& id);
    // 设置槽位索引（用于生成稳定的唯一 ID）
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
    mutable std::wstring m_name;        // 设备名称
    mutable std::wstring m_labelText;    // 标签文本
    mutable std::wstring m_valueText;    // 值文本
    mutable std::wstring m_itemId;      // 项目ID
    std::vector<DeviceBattery> m_selectedDevices;  // 选中的设备列表
    bool m_isOnline = false;             // 在线状态
    int  m_index = 0;                    // 槽位索引

    mutable std::mutex m_mutex;          // 线程安全互斥锁

    void RebuildText();  // 重建显示文本
};
