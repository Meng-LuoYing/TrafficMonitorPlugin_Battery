#pragma once
#include "PluginInterface.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

class CBatteryItem : public IPluginItem
{
public:
    CBatteryItem();
    ~CBatteryItem();

    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidth() const override;
    virtual void OnDrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    virtual void OnMouseEvent(UINT message, WPARAM wParam, LPARAM lParam) override;

    void SetValue(int batteryLevel, const std::wstring& deviceName);

private:
    std::wstring m_itemName;
    std::wstring m_itemId;
    std::wstring m_labelText;
    std::wstring m_valueText;
    std::wstring m_sampleValueText;
    int m_batteryLevel;
    mutable std::mutex m_mutex;
};

class CBatteryPlugin : public ITMPlugin
{
public:
    static CBatteryPlugin& Instance();

    virtual int GetAPIVersion() const override;
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual const wchar_t* GetInfo(int index) override;
    virtual void OnExtenedInfo(int index, const wchar_t* data) override;
    virtual const wchar_t* GetTooltipInfo() override;

private:
    CBatteryPlugin();
    ~CBatteryPlugin();
    CBatteryPlugin(const CBatteryPlugin&) = delete;
    CBatteryPlugin& operator=(const CBatteryPlugin&) = delete;

    void FetchBatteryStatus();
    void ParseJson(const std::string& json);

    CBatteryItem m_item;
    
    // WinHTTP session handle
    void* m_hSession;
    
    std::atomic<bool> m_fetching;
};
