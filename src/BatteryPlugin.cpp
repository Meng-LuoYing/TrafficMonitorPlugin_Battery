#include "pch.h"
#include "BatteryPlugin.h"
#include "HttpClient.h"
#include "JsonParser.h"

BatteryPlugin BatteryPlugin::m_instance;

BatteryPlugin::BatteryPlugin()
{
}

BatteryPlugin& BatteryPlugin::Instance()
{
    return m_instance;
}

void BatteryPlugin::InitDevices()
{
    if (m_initialized) return;
    m_initialized = true;
    FetchAndUpdate();
}

void BatteryPlugin::FetchAndUpdate()
{
    std::string json = HttpGet(API_HOST, API_PORT, API_PATH, 3000);
    if (json.empty())
    {
        for (auto& item : m_items)
            item->SetOffline();
        return;
    }

    auto devices = ParseBatteryJson(json);

    if (m_items.empty())
    {
        // First call: create one BatteryItem per device
        for (int i = 0; i < (int)devices.size(); i++)
        {
            auto item = std::make_unique<BatteryItem>();
            item->SetIndex(i);
            item->Update(devices[i]);
            m_items.push_back(std::move(item));
        }
    }
    else
    {
        // Subsequent calls: match by device ID and update
        for (auto& dev : devices)
        {
            std::wstring expectedId = L"Battery_" + dev.id;
            for (auto& item : m_items)
            {
                if (std::wstring(item->GetItemId()) == expectedId)
                {
                    item->Update(dev);
                    break;
                }
            }
        }

        // Mark any device that disappeared from the API response as offline
        for (auto& item : m_items)
        {
            bool found = false;
            for (auto& dev : devices)
            {
                if (std::wstring(item->GetItemId()) == L"Battery_" + dev.id)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                item->SetOffline();
        }
    }
}

IPluginItem* BatteryPlugin::GetItem(int index)
{
    if (!m_initialized)
        InitDevices();

    if (index >= 0 && index < (int)m_items.size())
        return m_items[index].get();
    return nullptr;
}

void BatteryPlugin::DataRequired()
{
    if (!m_initialized)
        InitDevices();
    else
        FetchAndUpdate();
}

const wchar_t* BatteryPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:        return L"BLE Battery";
    case TMI_DESCRIPTION: return L"Display BLE device battery via local REST API";
    case TMI_AUTHOR:      return L"User";
    case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
    case TMI_VERSION:     return L"1.0.0";
    case TMI_URL:         return L"http://127.0.0.1:18080";
    default:              return L"";
    }
}

void BatteryPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    (void)index; (void)data;
}

void BatteryPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_pApp = pApp;
}

ITMPlugin* TMPluginGetInstance()
{
    return &BatteryPlugin::Instance();
}
