#include "BatteryPlugin.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <thread>

#pragma comment(lib, "winhttp.lib")

// Helper function to convert UTF-8 string to wide string
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// CBatteryItem implementation

CBatteryItem::CBatteryItem()
    : m_itemName(L"设备电量"), m_itemId(L"DeviceBattery"), m_labelText(L"电量:"), m_valueText(L"--%"), m_sampleValueText(L"80%"), m_batteryLevel(-1)
{
}

CBatteryItem::~CBatteryItem()
{
}

const wchar_t* CBatteryItem::GetItemName() const
{
    // Item name is static, no lock needed
    return m_itemName.c_str();
}

const wchar_t* CBatteryItem::GetItemId() const
{
    // Item ID is static, no lock needed
    return m_itemId.c_str();
}

const wchar_t* CBatteryItem::GetItemLableText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_labelText.c_str();
}

const wchar_t* CBatteryItem::GetItemValueText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_valueText.c_str();
}

const wchar_t* CBatteryItem::GetItemValueSampleText() const
{
    return m_sampleValueText.c_str();
}

bool CBatteryItem::IsCustomDraw() const
{
    return false;
}

int CBatteryItem::GetItemWidth() const
{
    return 0;
}

void CBatteryItem::OnDrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
}

void CBatteryItem::OnMouseEvent(UINT message, WPARAM wParam, LPARAM lParam)
{
}

void CBatteryItem::SetValue(int batteryLevel, const std::wstring& deviceName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batteryLevel = batteryLevel;
    if (!deviceName.empty()) {
        m_labelText = deviceName + L":";
    }
    else {
        m_labelText = L"电量:";
    }

    if (batteryLevel >= 0) {
        m_valueText = std::to_wstring(batteryLevel) + L"%";
    }
    else {
        m_valueText = L"--%";
    }
}

// CBatteryPlugin implementation

CBatteryPlugin& CBatteryPlugin::Instance()
{
    static CBatteryPlugin instance;
    return instance;
}

CBatteryPlugin::CBatteryPlugin()
    : m_hSession(NULL), m_fetching(false)
{
    // Initialize WinHTTP session
    m_hSession = WinHttpOpen(L"TrafficMonitor Battery Plugin/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    
    // Set timeout to avoid hanging threads too long
    if (m_hSession) {
        WinHttpSetTimeouts(m_hSession, 2000, 2000, 2000, 2000); // 2s timeout
    }
}

CBatteryPlugin::~CBatteryPlugin()
{
    if (m_hSession) WinHttpCloseHandle(m_hSession);
}

int CBatteryPlugin::GetAPIVersion() const
{
    return 1;
}

IPluginItem* CBatteryPlugin::GetItem(int index)
{
    if (index == 0)
        return &m_item;
    return nullptr;
}

void CBatteryPlugin::DataRequired()
{
    if (!m_fetching) {
        m_fetching = true;
        std::thread([this]() {
            FetchBatteryStatus();
            m_fetching = false;
        }).detach();
    }
}

const wchar_t* CBatteryPlugin::GetInfo(int index)
{
    return L"";
}

void CBatteryPlugin::OnExtenedInfo(int index, const wchar_t* data)
{
}

const wchar_t* CBatteryPlugin::GetTooltipInfo()
{
    return L"从 http://127.0.0.1:18080/api/v1/status 获取电量信息";
}

void CBatteryPlugin::FetchBatteryStatus()
{
    HINTERNET hConnect = NULL, hRequest = NULL;

    // Connect
    if (m_hSession) {
        hConnect = WinHttpConnect(m_hSession, L"127.0.0.1", 18080, 0);
    }

    if (hConnect) {
        // Open request
        hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/v1/status",
            NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            0);

        if (hRequest) {
            // Send request
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0,
                0, 0)) {
                // Receive response
                if (WinHttpReceiveResponse(hRequest, NULL)) {
                    // Read data
                    std::string response;
                    DWORD dwSize = 0;
                    DWORD dwDownloaded = 0;
                    do {
                        // Check for available data.
                        dwSize = 0;
                        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                            break;
                        }
                        if (dwSize == 0) {
                            break;
                        }

                        // Allocate space for the buffer.
                        std::vector<char> buffer(dwSize + 1);
                        if (!WinHttpReadData(hRequest, &buffer[0], dwSize, &dwDownloaded)) {
                            break;
                        }
                        buffer[dwDownloaded] = '\0';
                        response += &buffer[0];
                    } while (dwSize > 0);

                    // Parse JSON
                    ParseJson(response);
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
}

// Simple JSON parser looking for keys
void CBatteryPlugin::ParseJson(const std::string& json)
{
    // Look for "name"
    std::string name;
    size_t namePos = json.find("\"name\"");
    if (namePos != std::string::npos) {
        size_t start = json.find("\"", namePos + 6); // Skip "name":
        if (start != std::string::npos) {
            start++; // Skip "
            size_t end = json.find("\"", start);
            if (end != std::string::npos) {
                name = json.substr(start, end - start);
            }
        }
    }

    // Look for "renamedName"
    std::string renamedName;
    size_t renamedPos = json.find("\"renamedName\"");
    if (renamedPos != std::string::npos) {
        size_t start = json.find("\"", renamedPos + 13); // Skip "renamedName":
        if (start != std::string::npos) {
            start++; // Skip "
            size_t end = json.find("\"", start);
            if (end != std::string::npos) {
                renamedName = json.substr(start, end - start);
            }
        }
    }

    // Look for battery level (assuming key "battery", "level", "charge", or "capacity")
    int battery = -1;
    // Prioritize exact matches with quotes
    std::vector<std::string> batteryKeys = { "\"battery\"", "\"level\"", "\"charge\"", "\"capacity\"" };
    
    for (const auto& key : batteryKeys) {
        size_t pos = json.find(key);
        if (pos != std::string::npos) {
            size_t start = json.find(":", pos);
            if (start != std::string::npos) {
                start++;
                // Skip whitespace
                while (start < json.length() && isspace(json[start])) start++;
                
                // Read number
                size_t end = start;
                while (end < json.length() && (isdigit(json[end]) || json[end] == '.')) end++;
                
                if (end > start) {
                    try {
                        // Check if it's a float, if so cast to int
                        std::string numStr = json.substr(start, end - start);
                        battery = (int)std::stod(numStr);
                    } catch (...) {}
                }
                if (battery != -1) break;
            }
        }
    }

    // Update item
    std::wstring deviceLabel = L"设备";
    if (!renamedName.empty()) {
        deviceLabel = Utf8ToWide(renamedName);
    } else if (!name.empty()) {
        deviceLabel = Utf8ToWide(name);
    }
    
    m_item.SetValue(battery, deviceLabel);
}

// Exported function
ITMPlugin* TMPluginGetInstance()
{
    return &CBatteryPlugin::Instance();
}
