#pragma once
#include "../include/PluginInterface.h"
#include "BatteryItem.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <set>

// 电池插件主类：实现 ITMPlugin 接口
class BatteryPlugin : public ITMPlugin
{
private:
    BatteryPlugin();

public:
    static BatteryPlugin& Instance();

    // ITMPlugin 接口实现
    virtual IPluginItem* GetItem(int index) override;                    // 获取指定索引的插件项目
    virtual void DataRequired() override;                               // 数据获取回调
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;     // 获取插件信息
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override; // 扩展信息处理
    virtual void OnInitialize(ITrafficMonitor* pApp) override;         // 初始化
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;     // 显示选项对话框

    // 设备选择相关的公共方法
    std::vector<DeviceBattery> GetAvailableDevices() const;            // 获取可用设备列表
    void SetDeviceSelection(const std::wstring& deviceId, bool selected); // 设置设备选择状态
    bool IsDeviceSelected(const std::wstring& deviceId) const;          // 检查设备是否被选中
    void RefreshDevicesNow(); // 强制立即获取 API 数据并更新设备列表

private:
    void InitDevices();                    // 初始化设备
    void FetchAndUpdate(bool syncDevices); // 获取并更新数据
    void LoadConfig();                      // 加载配置
    void SaveConfig();                      // 保存配置
    void UpdateConfigDir(const wchar_t* dir); // 更新配置目录
    std::wstring GetConfigPath() const;     // 获取配置文件路径
    void RebuildItems(); // 严格基于 m_selectedDevices 重建 m_items 向量

private:
    static BatteryPlugin m_instance;        // 单例实例

    std::vector<std::unique_ptr<BatteryItem>> m_items;  // 插件项目列表
    std::unique_ptr<BatteryItem> m_displayItem;         // 显示项目
    std::set<std::wstring> m_selectedDevices;           // 选中的设备ID集合
    std::vector<DeviceBattery> m_availableDevices;      // 可用设备列表
    bool m_initialized = false;                         // 是否已初始化
    bool m_autoSelectFirstDevices = false;              // 是否自动选择前几个设备
    ITrafficMonitor* m_pApp = nullptr;                 // TrafficMonitor 应用指针
    int m_apiPort = 18080;                              // API 端口
    std::wstring m_apiToken;                            // API 令牌
    int m_authFailCount = 0;                            // 鉴权失败次数
    bool m_pluginDisabled = false;                       // 插件是否被禁用
    int m_deviceSyncIntervalMs = 5000;                  // 设备同步间隔（毫秒）
    int m_batteryRefreshIntervalMs = 2000;               // 电量刷新间隔（毫秒）
    unsigned long long m_lastDeviceSyncTick = 0;        // 上次设备同步时间
    unsigned long long m_lastBatteryRefreshTick = 0;     // 上次电量刷新时间
    std::wstring m_configDir;                            // 配置目录
    mutable std::mutex m_mutex;                          // 线程安全互斥锁

    static constexpr const wchar_t* API_HOST = L"127.0.0.1"; // API 主机地址
    static constexpr const wchar_t* API_PATH = L"/api/v1/status"; // API 路径
};

#ifdef __cplusplus
extern "C" {
#endif
    // 插件导出函数
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
