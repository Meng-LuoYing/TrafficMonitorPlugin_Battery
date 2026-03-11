#pragma once
#include <Windows.h>

// 插件接口版本
// Plugin interface version
#define TM_PLUGIN_API_VERSION 1

class IPluginItem
{
public:
    // 获取显示项目的名称
    // Get the name of the display item
    virtual const wchar_t* GetItemName() const = 0;

    // 获取显示项目的唯一ID
    // Get the unique ID of the display item
    virtual const wchar_t* GetItemId() const = 0;

    // 获取显示项目的标签文本
    // Get the label text of the display item
    virtual const wchar_t* GetItemLableText() const = 0;

    // 获取显示项目的值文本
    // Get the value text of the display item
    virtual const wchar_t* GetItemValueText() const = 0;

    // 获取显示项目的值的示例文本（用于设置界面中预览）
    // Get the sample text of the value of the display item (used for preview in the settings dialog)
    virtual const wchar_t* GetItemValueSampleText() const = 0;

    // 是否自定义绘制
    // Whether to custom draw
    virtual bool IsCustomDraw() const = 0;

    // 获取显示项目的宽度（仅在自定义绘制时有效）
    // Get the width of the display item (only valid when custom draw)
    virtual int GetItemWidth() const = 0;

    // 自定义绘制函数
    // Custom draw function
    virtual void OnDrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) = 0;

    // 鼠标事件处理函数
    // Mouse event handler
    virtual void OnMouseEvent(UINT message, WPARAM wParam, LPARAM lParam) = 0;
};

class ITMPlugin
{
public:
    // 获取API版本
    // Get API version
    virtual int GetAPIVersion() const = 0;

    // 获取显示项目
    // Get display item
    virtual IPluginItem* GetItem(int index) = 0;

    // 数据更新
    // Data update
    virtual void DataRequired() = 0;

    // 获取插件信息
    // Get plugin info
    virtual const wchar_t* GetInfo(int index) = 0;

    // 接收扩展信息
    // Receive extended info
    virtual void OnExtenedInfo(int index, const wchar_t* data) = 0;
    
    // 获取提示信息
    // Get tooltip info
    virtual const wchar_t* GetTooltipInfo() { return L""; }
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
