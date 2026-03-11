#include "pch.h"
#include "BatteryPlugin.h"
#include "HttpClient.h"
#include "JsonParser.h"
#include <cwchar>

namespace
{
    struct PortDialogState
    {
        int currentPort = 18080;
        int resultPort = 18080;
        bool accepted = false;
        HWND hEdit = nullptr;
    };

    constexpr int ID_PORT_EDIT = 1001;
    constexpr int DIALOG_WIDTH = 320;
    constexpr int DIALOG_HEIGHT = 150;

    LRESULT CALLBACK PortDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }

        auto* state = reinterpret_cast<PortDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_CREATE:
        {
            if (!state) return -1;
            CreateWindowW(L"STATIC", L"请输入 API 端口 (1-65535)：",
                WS_CHILD | WS_VISIBLE, 16, 16, 260, 20, hWnd, nullptr, nullptr, nullptr);

            wchar_t portText[16] = {};
            wsprintfW(portText, L"%d", state->currentPort);
            state->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", portText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 40, 280, 24,
                hWnd, (HMENU)(INT_PTR)ID_PORT_EDIT, nullptr, nullptr);
            SendMessageW(state->hEdit, EM_SETLIMITTEXT, 5, 0);

            CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                128, 86, 80, 26, hWnd, (HMENU)(INT_PTR)IDOK, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
                216, 86, 80, 26, hWnd, (HMENU)(INT_PTR)IDCANCEL, nullptr, nullptr);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam && state && state->hEdit)
            {
                SetFocus(state->hEdit);
                SendMessageW(state->hEdit, EM_SETSEL, 0, -1);
            }
            return 0;
        case WM_COMMAND:
        {
            const int cmd = LOWORD(wParam);
            if (cmd == IDOK && state && state->hEdit)
            {
                wchar_t text[32] = {};
                GetWindowTextW(state->hEdit, text, 31);
                wchar_t* end = nullptr;
                long value = wcstol(text, &end, 10);
                if (text[0] == L'\0' || end == text || *end != L'\0' || value < 1 || value > 65535)
                {
                    MessageBoxW(hWnd, L"端口必须是 1 到 65535 之间的整数。", L"蓝牙电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hEdit);
                    SendMessageW(state->hEdit, EM_SETSEL, 0, -1);
                    return 0;
                }
                state->resultPort = static_cast<int>(value);
                state->accepted = true;
                DestroyWindow(hWnd);
                return 0;
            }
            if (cmd == IDCANCEL)
            {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool ShowPortDialog(HWND parent, int currentPort, int& resultPort)
    {
        static const wchar_t* CLASS_NAME = L"BatteryPluginPortDialog";
        static ATOM classAtom = 0;
        if (classAtom == 0)
        {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = PortDialogProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = CLASS_NAME;
            classAtom = RegisterClassW(&wc);
            if (classAtom == 0)
                return false;
        }

        RECT rcParent = {};
        if (parent == nullptr || !GetWindowRect(parent, &rcParent))
        {
            rcParent.left = 0;
            rcParent.top = 0;
            rcParent.right = GetSystemMetrics(SM_CXSCREEN);
            rcParent.bottom = GetSystemMetrics(SM_CYSCREEN);
            parent = nullptr;
        }

        int x = rcParent.left + ((rcParent.right - rcParent.left) - DIALOG_WIDTH) / 2;
        int y = rcParent.top + ((rcParent.bottom - rcParent.top) - DIALOG_HEIGHT) / 2;

        PortDialogState state;
        state.currentPort = currentPort;
        state.resultPort = currentPort;

        if (parent) EnableWindow(parent, FALSE);
        HWND hWnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            CLASS_NAME,
            L"蓝牙电量 插件选项",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            x, y, DIALOG_WIDTH, DIALOG_HEIGHT,
            parent, nullptr, GetModuleHandleW(nullptr), &state);

        if (!hWnd)
        {
            if (parent) EnableWindow(parent, TRUE);
            return false;
        }

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        MSG msg = {};
        BOOL ret = 1;
        while (IsWindow(hWnd) && (ret = GetMessageW(&msg, nullptr, 0, 0)) > 0)
        {
            if (!IsDialogMessageW(hWnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (parent)
        {
            EnableWindow(parent, TRUE);
            SetActiveWindow(parent);
        }

        if (ret == 0)
            PostQuitMessage(static_cast<int>(msg.wParam));

        if (state.accepted)
        {
            resultPort = state.resultPort;
            return true;
        }
        return false;
    }
}

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
    int port = 18080;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
    }
    std::string json = HttpGet(API_HOST, port, API_PATH, 3000);
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
    case TMI_NAME:        return L"设备电量";
    case TMI_DESCRIPTION: return L"通过本地 REST API 显示设备电量";
    case TMI_AUTHOR:      return L"梦落影逝";
    case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
    case TMI_VERSION:     return L"1.0.0";
    case TMI_URL:
    {
        static thread_local std::wstring url;
        int port = 18080;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            port = m_apiPort;
        }
        url = L"http://127.0.0.1:" + std::to_wstring(port);
        return url.c_str();
    }
    default:              return L"";
    }
}

void BatteryPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data != nullptr && data[0] != L'\0')
        UpdateConfigDir(data);
}

void BatteryPlugin::OnInitialize(ITrafficMonitor* pApp)
{
    m_pApp = pApp;
    if (pApp != nullptr)
    {
        const wchar_t* configDir = pApp->GetPluginConfigDir();
        if (configDir != nullptr && configDir[0] != L'\0')
            UpdateConfigDir(configDir);
    }
}

ITMPlugin::OptionReturn BatteryPlugin::ShowOptionsDialog(void* hParent)
{
    int currentPort = 18080;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        currentPort = m_apiPort;
    }

    int newPort = currentPort;
    bool accepted = ShowPortDialog(reinterpret_cast<HWND>(hParent), currentPort, newPort);
    if (!accepted || newPort == currentPort)
        return OR_OPTION_UNCHANGED;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_apiPort = newPort;
    }
    SaveConfig();
    FetchAndUpdate();
    return OR_OPTION_CHANGED;
}

std::wstring BatteryPlugin::GetConfigPath() const
{
    std::wstring configDir;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        configDir = m_configDir;
    }
    if (configDir.empty() && m_pApp != nullptr)
    {
        const wchar_t* dir = m_pApp->GetPluginConfigDir();
        if (dir != nullptr)
            configDir = dir;
    }
    if (configDir.empty())
        return {};

    wchar_t last = configDir.back();
    if (last != L'\\' && last != L'/')
        configDir += L'\\';
    return configDir + L"BatteryPlugin.ini";
}

void BatteryPlugin::LoadConfig()
{
    std::wstring configPath = GetConfigPath();
    if (configPath.empty())
        return;

    int fallback = 18080;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fallback = m_apiPort;
    }
    int port = GetPrivateProfileIntW(L"network", L"port", fallback, configPath.c_str());
    if (port >= 1 && port <= 65535)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_apiPort = port;
    }
}

void BatteryPlugin::SaveConfig()
{
    std::wstring configPath = GetConfigPath();
    if (configPath.empty())
        return;

    int port = 18080;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
    }
    wchar_t text[16] = {};
    wsprintfW(text, L"%d", port);
    WritePrivateProfileStringW(L"network", L"port", text, configPath.c_str());
}

void BatteryPlugin::UpdateConfigDir(const wchar_t* dir)
{
    if (dir == nullptr || dir[0] == L'\0')
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_configDir = dir;
    }
    LoadConfig();
}

ITMPlugin* TMPluginGetInstance()
{
    return &BatteryPlugin::Instance();
}
