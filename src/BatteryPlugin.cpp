#include "pch.h"
#include "BatteryPlugin.h"
#include "HttpClient.h"
#include "JsonParser.h"
#include <cwchar>
#include <shellapi.h>

namespace
{
    struct OptionsDialogState
    {
        int currentPort = 18080;
        int resultPort = 18080;
        std::wstring currentToken;
        std::wstring resultToken;
        int currentDeviceSyncSec = 5;
        int resultDeviceSyncSec = 5;
        int currentBatteryRefreshSec = 2;
        int resultBatteryRefreshSec = 2;
        bool accepted = false;
        HWND hPortEdit = nullptr;
        HWND hTokenEdit = nullptr;
        HWND hDeviceSyncEdit = nullptr;
        HWND hBatteryRefreshEdit = nullptr;
        HWND hNetworkGroup = nullptr;
        HWND hTimingGroup = nullptr;
        HWND hPortLabel = nullptr;
        HWND hTokenLabel = nullptr;
        HWND hDeviceSyncLabel = nullptr;
        HWND hBatteryRefreshLabel = nullptr;
        HWND hOkButton = nullptr;
        HWND hCancelButton = nullptr;
        HWND hApplyButton = nullptr;
        HFONT hFont = nullptr;
        bool applied = false;
    };

    constexpr int ID_PORT_EDIT = 1001;
    constexpr int ID_TOKEN_EDIT = 1002;
    constexpr int ID_DEVICE_SYNC_EDIT = 1003;
    constexpr int ID_BATTERY_REFRESH_EDIT = 1004;
    constexpr int ID_APPLY_BUTTON = 1005;
    constexpr int ID_TOKEN_PROMPT_EDIT = 1101;
    constexpr int DIALOG_WIDTH = 520;
    constexpr int DIALOG_HEIGHT = 340;

    bool TryParseInteger(const wchar_t* text, int minValue, int maxValue, int& out)
    {
        if (text == nullptr || text[0] == L'\0')
            return false;
        wchar_t* end = nullptr;
        long value = wcstol(text, &end, 10);
        if (end == text || *end != L'\0' || value < minValue || value > maxValue)
            return false;
        out = static_cast<int>(value);
        return true;
    }

    bool IsAuthFailedResponse(const std::string& json)
    {
        size_t keyPos = json.find("\"code\"");
        if (keyPos == std::string::npos)
            return false;
        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos)
            return false;
        size_t p = colonPos + 1;
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n'))
            ++p;
        size_t end = p;
        while (end < json.size() && json[end] >= '0' && json[end] <= '9')
            ++end;
        if (end == p)
            return false;
        return (json.substr(p, end - p) == "401");
    }

    void RestartTrafficMonitor()
    {
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
            return;

        std::wstring command = L"/c ping 127.0.0.1 -n 2 >nul & start \"\" \"";
        command += exePath;
        command += L"\"";

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::wstring cmdLine = L"cmd.exe ";
        cmdLine += command;
        if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            ExitProcess(0);
        }
    }

    struct TokenPromptState
    {
        std::wstring currentToken;
        std::wstring resultToken;
        bool accepted = false;
        HWND hEdit = nullptr;
        HFONT hFont = nullptr;
        int failedCount = 0;
    };

    LRESULT CALLBACK TokenPromptProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }

        auto* state = reinterpret_cast<TokenPromptState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_CREATE:
        {
            if (!state) return -1;
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
                state->hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (!state->hFont)
                state->hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            HWND hGroup = CreateWindowW(L"BUTTON", L"鉴权设置",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 12, 12, 396, 140, hWnd, nullptr, nullptr, nullptr);
            wchar_t tip[128] = {};
            wsprintfW(tip, L"鉴权失败，请输入 Token（第 %d/3 次）", state->failedCount);
            HWND hLabel = CreateWindowW(L"STATIC", tip, WS_CHILD | WS_VISIBLE, 24, 36, 372, 20, hWnd, nullptr, nullptr, nullptr);
            state->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->currentToken.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 70, 372, 26, hWnd, (HMENU)(INT_PTR)ID_TOKEN_PROMPT_EDIT, nullptr, nullptr);
            SendMessageW(state->hEdit, EM_SETLIMITTEXT, 256, 0);

            HWND hOk = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 240, 164, 80, 28, hWnd, (HMENU)(INT_PTR)IDOK, nullptr, nullptr);
            HWND hCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, 328, 164, 80, 28, hWnd, (HMENU)(INT_PTR)IDCANCEL, nullptr, nullptr);

            SendMessageW(hGroup, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hEdit, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(hOk, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(hCancel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
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
            if (!state) return 0;
            if (LOWORD(wParam) == IDOK)
            {
                wchar_t tokenText[512] = {};
                GetWindowTextW(state->hEdit, tokenText, 511);
                state->resultToken = tokenText;
                state->accepted = true;
                DestroyWindow(hWnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_DESTROY:
            if (state && state->hFont && state->hFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
            {
                DeleteObject(state->hFont);
                state->hFont = nullptr;
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool PromptTokenDialog(HWND parent, const std::wstring& currentToken, int failedCount, std::wstring& tokenOut)
    {
        static const wchar_t* CLASS_NAME = L"BatteryPluginTokenPromptDialog";
        static ATOM classAtom = 0;
        if (classAtom == 0)
        {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = TokenPromptProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = CLASS_NAME;
            classAtom = RegisterClassW(&wc);
            if (classAtom == 0)
                return false;
        }

        HWND ownerWindow = nullptr;
        if (parent && IsWindow(parent))
        {
            ownerWindow = parent;
        }
        else
        {
            HWND fg = GetForegroundWindow();
            if (fg && IsWindow(fg))
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(fg, &pid);
                if (pid == GetCurrentProcessId())
                    ownerWindow = fg;
            }
        }
        RECT rcWork = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        HMONITOR hMonitor = nullptr;
        if (ownerWindow)
            hMonitor = MonitorFromWindow(ownerWindow, MONITOR_DEFAULTTONEAREST);
        else
        {
            POINT pt = {};
            GetCursorPos(&pt);
            hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        }
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (hMonitor && GetMonitorInfoW(hMonitor, &mi))
            rcWork = mi.rcWork;

        int width = 430;
        int height = 240;
        int x = rcWork.left + ((rcWork.right - rcWork.left) - width) / 2;
        int y = rcWork.top + ((rcWork.bottom - rcWork.top) - height) / 2;

        TokenPromptState state;
        state.currentToken = currentToken;
        state.resultToken = currentToken;
        state.failedCount = failedCount;

        if (ownerWindow) EnableWindow(ownerWindow, FALSE);
        HWND hWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, CLASS_NAME, L"设备电量 鉴权", WS_POPUP | WS_CAPTION | WS_SYSMENU,
            x, y, width, height, ownerWindow, nullptr, GetModuleHandleW(nullptr), &state);
        if (!hWnd)
        {
            if (ownerWindow) EnableWindow(ownerWindow, TRUE);
            return false;
        }

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);
        MSG msg = {};
        while (IsWindow(hWnd) && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(hWnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetActiveWindow(ownerWindow);
        }

        if (state.accepted)
        {
            tokenOut = state.resultToken;
            return true;
        }
        return false;
    }

    LRESULT CALLBACK PortDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }

        auto* state = reinterpret_cast<OptionsDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_CREATE:
        {
            if (!state) return -1;
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            {
                state->hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            }
            if (!state->hFont)
            {
                state->hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            }

            state->hNetworkGroup = CreateWindowW(L"BUTTON", L"网络设置",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 16, 16, 488, 122, hWnd, nullptr, nullptr, nullptr);
            state->hTimingGroup = CreateWindowW(L"BUTTON", L"刷新设置",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 16, 148, 488, 124, hWnd, nullptr, nullptr, nullptr);

            state->hPortLabel = CreateWindowW(L"STATIC", L"API 端口：",
                WS_CHILD | WS_VISIBLE, 32, 46, 100, 20, hWnd, nullptr, nullptr, nullptr);
            state->hTokenLabel = CreateWindowW(L"STATIC", L"Token：",
                WS_CHILD | WS_VISIBLE, 32, 90, 100, 20, hWnd, nullptr, nullptr, nullptr);
            state->hDeviceSyncLabel = CreateWindowW(L"STATIC", L"设备变动检测间隔（秒，1-3600）：",
                WS_CHILD | WS_VISIBLE, 32, 178, 250, 20, hWnd, nullptr, nullptr, nullptr);
            state->hBatteryRefreshLabel = CreateWindowW(L"STATIC", L"电量刷新间隔（秒，1-3600）：",
                WS_CHILD | WS_VISIBLE, 32, 222, 250, 20, hWnd, nullptr, nullptr, nullptr);

            wchar_t portText[16] = {};
            wchar_t syncText[16] = {};
            wchar_t refreshText[16] = {};
            wsprintfW(portText, L"%d", state->currentPort);
            wsprintfW(syncText, L"%d", state->currentDeviceSyncSec);
            wsprintfW(refreshText, L"%d", state->currentBatteryRefreshSec);

            state->hPortEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", portText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 132, 42, 352, 24,
                hWnd, (HMENU)(INT_PTR)ID_PORT_EDIT, nullptr, nullptr);
            state->hTokenEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->currentToken.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 132, 86, 352, 24,
                hWnd, (HMENU)(INT_PTR)ID_TOKEN_EDIT, nullptr, nullptr);
            state->hDeviceSyncEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", syncText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 174, 194, 24,
                hWnd, (HMENU)(INT_PTR)ID_DEVICE_SYNC_EDIT, nullptr, nullptr);
            state->hBatteryRefreshEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", refreshText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 218, 194, 24,
                hWnd, (HMENU)(INT_PTR)ID_BATTERY_REFRESH_EDIT, nullptr, nullptr);
            SendMessageW(state->hPortEdit, EM_SETLIMITTEXT, 5, 0);
            SendMessageW(state->hTokenEdit, EM_SETLIMITTEXT, 256, 0);
            SendMessageW(state->hDeviceSyncEdit, EM_SETLIMITTEXT, 4, 0);
            SendMessageW(state->hBatteryRefreshEdit, EM_SETLIMITTEXT, 4, 0);

            state->hOkButton = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                332, 296, 80, 28, hWnd, (HMENU)(INT_PTR)IDOK, nullptr, nullptr);
            state->hCancelButton = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
                420, 296, 80, 28, hWnd, (HMENU)(INT_PTR)IDCANCEL, nullptr, nullptr);
            state->hApplyButton = CreateWindowW(L"BUTTON", L"应用", WS_CHILD | WS_VISIBLE,
                244, 296, 80, 28, hWnd, (HMENU)(INT_PTR)ID_APPLY_BUTTON, nullptr, nullptr);

            SendMessageW(state->hNetworkGroup, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hTimingGroup, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hPortLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hTokenLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hDeviceSyncLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hBatteryRefreshLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hPortEdit, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hTokenEdit, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hDeviceSyncEdit, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hBatteryRefreshEdit, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hOkButton, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hCancelButton, WM_SETFONT, (WPARAM)state->hFont, TRUE);
            SendMessageW(state->hApplyButton, WM_SETFONT, (WPARAM)state->hFont, TRUE);

            RECT rc = {};
            GetClientRect(hWnd, &rc);
            SendMessageW(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
            return 0;
        }
        case WM_SIZE:
        {
            if (!state) return 0;
            int clientW = LOWORD(lParam);
            int clientH = HIWORD(lParam);
            int margin = 16;
            int groupW = clientW - margin * 2;
            if (groupW < 420) groupW = 420;
            int buttonW = 80;
            int buttonH = 28;
            int buttonGap = 8;
            int buttonY = clientH - buttonH - 14;
            int right = clientW - 20;
            int networkTop = 16;
            int networkHeight = 122;
            int timingTop = networkTop + networkHeight + 10;
            int timingHeight = buttonY - 12 - timingTop;
            if (timingHeight < 108) timingHeight = 108;
            int labelLeft = 32;
            int valueLeft = 290;
            int valueW = groupW - (valueLeft - margin) - 20;
            if (valueW < 160) valueW = 160;

            SetWindowPos(state->hNetworkGroup, nullptr, margin, networkTop, groupW, networkHeight, SWP_NOZORDER);
            SetWindowPos(state->hTimingGroup, nullptr, margin, timingTop, groupW, timingHeight, SWP_NOZORDER);
            SetWindowPos(state->hPortLabel, nullptr, labelLeft, networkTop + 26, 100, 20, SWP_NOZORDER);
            SetWindowPos(state->hPortEdit, nullptr, 132, networkTop + 22, groupW - 132 - 20, 24, SWP_NOZORDER);
            SetWindowPos(state->hTokenLabel, nullptr, labelLeft, networkTop + 70, 100, 20, SWP_NOZORDER);
            SetWindowPos(state->hTokenEdit, nullptr, 132, networkTop + 66, groupW - 132 - 20, 24, SWP_NOZORDER);
            SetWindowPos(state->hDeviceSyncLabel, nullptr, labelLeft, timingTop + 26, 250, 20, SWP_NOZORDER);
            SetWindowPos(state->hDeviceSyncEdit, nullptr, valueLeft, timingTop + 22, valueW, 24, SWP_NOZORDER);
            SetWindowPos(state->hBatteryRefreshLabel, nullptr, labelLeft, timingTop + 70, 250, 20, SWP_NOZORDER);
            SetWindowPos(state->hBatteryRefreshEdit, nullptr, valueLeft, timingTop + 66, valueW, 24, SWP_NOZORDER);

            SetWindowPos(state->hCancelButton, nullptr, right - buttonW, buttonY, buttonW, buttonH, SWP_NOZORDER);
            SetWindowPos(state->hOkButton, nullptr, right - buttonW * 2 - buttonGap, buttonY, buttonW, buttonH, SWP_NOZORDER);
            SetWindowPos(state->hApplyButton, nullptr, right - buttonW * 3 - buttonGap * 2, buttonY, buttonW, buttonH, SWP_NOZORDER);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam && state && state->hPortEdit)
            {
                RECT rc = {};
                GetClientRect(hWnd, &rc);
                SendMessageW(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
                SetFocus(state->hPortEdit);
                SendMessageW(state->hPortEdit, EM_SETSEL, 0, -1);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_COMMAND:
        {
            const int cmd = LOWORD(wParam);
            if ((cmd == IDOK || cmd == ID_APPLY_BUTTON) && state && state->hPortEdit && state->hTokenEdit && state->hDeviceSyncEdit && state->hBatteryRefreshEdit)
            {
                wchar_t portText[32] = {};
                wchar_t tokenText[512] = {};
                wchar_t syncText[32] = {};
                wchar_t refreshText[32] = {};
                GetWindowTextW(state->hPortEdit, portText, 31);
                GetWindowTextW(state->hTokenEdit, tokenText, 511);
                GetWindowTextW(state->hDeviceSyncEdit, syncText, 31);
                GetWindowTextW(state->hBatteryRefreshEdit, refreshText, 31);

                int parsedPort = 0;
                if (!TryParseInteger(portText, 1, 65535, parsedPort))
                {
                    MessageBoxW(hWnd, L"端口必须是 1 到 65535 之间的整数。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hPortEdit);
                    SendMessageW(state->hPortEdit, EM_SETSEL, 0, -1);
                    return 0;
                }

                int parsedSyncSec = 0;
                if (!TryParseInteger(syncText, 1, 3600, parsedSyncSec))
                {
                    MessageBoxW(hWnd, L"设备变动检测间隔必须是 1 到 3600 之间的整数秒。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hDeviceSyncEdit);
                    SendMessageW(state->hDeviceSyncEdit, EM_SETSEL, 0, -1);
                    return 0;
                }

                int parsedRefreshSec = 0;
                if (!TryParseInteger(refreshText, 1, 3600, parsedRefreshSec))
                {
                    MessageBoxW(hWnd, L"电量刷新间隔必须是 1 到 3600 之间的整数秒。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hBatteryRefreshEdit);
                    SendMessageW(state->hBatteryRefreshEdit, EM_SETSEL, 0, -1);
                    return 0;
                }

                state->resultPort = parsedPort;
                state->resultToken = tokenText;
                state->resultDeviceSyncSec = parsedSyncSec;
                state->resultBatteryRefreshSec = parsedRefreshSec;
                state->applied = true;
                state->accepted = true;
                if (cmd == IDOK)
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
        case WM_DESTROY:
            if (state && state->hFont && state->hFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
            {
                DeleteObject(state->hFont);
                state->hFont = nullptr;
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool ShowOptionsDialogWindow(HWND parent, int currentPort, const std::wstring& currentToken, int currentDeviceSyncSec, int currentBatteryRefreshSec, int& resultPort, std::wstring& resultToken, int& resultDeviceSyncSec, int& resultBatteryRefreshSec)
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

        HWND ownerWindow = (parent && IsWindow(parent)) ? parent : nullptr;
        HMONITOR hMonitor = nullptr;
        if (ownerWindow)
            hMonitor = MonitorFromWindow(ownerWindow, MONITOR_DEFAULTTONEAREST);
        else
        {
            POINT pt = {};
            GetCursorPos(&pt);
            hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        }

        RECT rcWork = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (hMonitor && GetMonitorInfoW(hMonitor, &mi))
            rcWork = mi.rcWork;

        int x = rcWork.left + ((rcWork.right - rcWork.left) - DIALOG_WIDTH) / 2;
        int y = rcWork.top + ((rcWork.bottom - rcWork.top) - DIALOG_HEIGHT) / 2;
        if (x < rcWork.left) x = rcWork.left;
        if (y < rcWork.top) y = rcWork.top;

        OptionsDialogState state;
        state.currentPort = currentPort;
        state.resultPort = currentPort;
        state.currentToken = currentToken;
        state.resultToken = currentToken;
        state.currentDeviceSyncSec = currentDeviceSyncSec;
        state.resultDeviceSyncSec = currentDeviceSyncSec;
        state.currentBatteryRefreshSec = currentBatteryRefreshSec;
        state.resultBatteryRefreshSec = currentBatteryRefreshSec;

        if (ownerWindow) EnableWindow(ownerWindow, FALSE);
        HWND hWnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            CLASS_NAME,
            L"设备电量 插件选项",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
            x, y, DIALOG_WIDTH, DIALOG_HEIGHT,
            ownerWindow, nullptr, GetModuleHandleW(nullptr), &state);

        if (!hWnd)
        {
            if (ownerWindow) EnableWindow(ownerWindow, TRUE);
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

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetActiveWindow(ownerWindow);
        }

        if (ret == 0)
            PostQuitMessage(static_cast<int>(msg.wParam));

        if (state.accepted)
        {
            resultPort = state.resultPort;
            resultToken = state.resultToken;
            resultDeviceSyncSec = state.resultDeviceSyncSec;
            resultBatteryRefreshSec = state.resultBatteryRefreshSec;
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
    FetchAndUpdate(true);
    unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastDeviceSyncTick = now;
        m_lastBatteryRefreshTick = now;
    }
}

void BatteryPlugin::FetchAndUpdate(bool syncDevices)
{
    int port = 18080;
    std::wstring token;
    bool disabled = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
        token = m_apiToken;
        disabled = m_pluginDisabled;
    }
    if (disabled)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_items)
            item->SetOffline();
        return;
    }
    std::string json = HttpGet(API_HOST, port, API_PATH, token.empty() ? nullptr : token.c_str(), 3000);
    if (json.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_items)
            item->SetOffline();
        return;
    }

    if (IsAuthFailedResponse(json))
    {
        int failCount = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            failCount = m_authFailCount;
            for (auto& item : m_items)
                item->SetOffline();
        }

        if (failCount >= 3)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pluginDisabled = true;
            }
            MessageBoxW(nullptr, L"连续验证失败 3 次，插件已停止加载。请在插件选项中修改 Token 后重新启用。", L"设备电量", MB_OK | MB_ICONERROR);
            return;
        }

        std::wstring editingToken = token;
        while (failCount < 3)
        {
            std::wstring inputToken = editingToken;
            bool accepted = PromptTokenDialog(nullptr, editingToken, failCount + 1, inputToken);
            if (!accepted)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pluginDisabled = true;
                return;
            }

            std::string verifyJson = HttpGet(API_HOST, port, API_PATH, inputToken.empty() ? nullptr : inputToken.c_str(), 3000);
            if (verifyJson.empty())
            {
                MessageBoxW(nullptr, L"验证 Token 失败：接口不可达，请稍后重试。", L"设备电量", MB_OK | MB_ICONWARNING);
                return;
            }
            if (IsAuthFailedResponse(verifyJson))
            {
                failCount++;
                editingToken = inputToken;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_authFailCount = failCount;
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_apiToken = inputToken;
                m_authFailCount = 0;
                m_pluginDisabled = false;
            }
            SaveConfig();
            RestartTrafficMonitor();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pluginDisabled = true;
        }
        MessageBoxW(nullptr, L"Token 连续验证失败 3 次，插件已停止加载。请在插件选项中修改 Token 后重新启用。", L"设备电量", MB_OK | MB_ICONERROR);
        return;
    }

    auto devices = ParseBatteryJson(json);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_authFailCount = 0;
    m_pluginDisabled = false;

    if (syncDevices)
    {
        for (auto& dev : devices)
        {
            std::wstring expectedId = L"Battery_" + dev.id;
            BatteryItem* matched = nullptr;
            for (auto& item : m_items)
            {
                if (std::wstring(item->GetItemId()) == expectedId)
                {
                    matched = item.get();
                    break;
                }
            }
            if (matched != nullptr)
            {
                matched->Update(dev);
            }
            else
            {
                auto item = std::make_unique<BatteryItem>();
                item->Update(dev);
                m_items.push_back(std::move(item));
            }
        }

        for (size_t i = 0; i < m_items.size();)
        {
            bool found = false;
            std::wstring itemId = m_items[i]->GetItemId();
            for (auto& dev : devices)
            {
                if (itemId == L"Battery_" + dev.id)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                m_items[i]->SetOffline();
            ++i;
        }

        for (int i = 0; i < (int)m_items.size(); ++i)
            m_items[i]->SetIndex(i);
    }
    else
    {
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

    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= 0 && index < (int)m_items.size())
        return m_items[index].get();
    return nullptr;
}

void BatteryPlugin::DataRequired()
{
    if (!m_initialized)
        InitDevices();
    else
    {
        unsigned long long now = GetTickCount64();
        bool needDeviceSync = false;
        bool needBatteryRefresh = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            unsigned long long deviceElapsed = now - m_lastDeviceSyncTick;
            unsigned long long refreshElapsed = now - m_lastBatteryRefreshTick;
            needDeviceSync = (deviceElapsed >= (unsigned long long)m_deviceSyncIntervalMs);
            needBatteryRefresh = (refreshElapsed >= (unsigned long long)m_batteryRefreshIntervalMs);
            if (needDeviceSync)
            {
                m_lastDeviceSyncTick = now;
                m_lastBatteryRefreshTick = now;
            }
            else if (needBatteryRefresh)
            {
                m_lastBatteryRefreshTick = now;
            }
        }

        if (needDeviceSync)
            FetchAndUpdate(true);
        else if (needBatteryRefresh)
            FetchAndUpdate(false);
    }
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
    std::wstring currentToken;
    int currentDeviceSyncSec = 5;
    int currentBatteryRefreshSec = 2;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        currentPort = m_apiPort;
        currentToken = m_apiToken;
        currentDeviceSyncSec = m_deviceSyncIntervalMs / 1000;
        currentBatteryRefreshSec = m_batteryRefreshIntervalMs / 1000;
    }

    int newPort = currentPort;
    std::wstring newToken = currentToken;
    int newDeviceSyncSec = currentDeviceSyncSec;
    int newBatteryRefreshSec = currentBatteryRefreshSec;
    bool accepted = ShowOptionsDialogWindow(reinterpret_cast<HWND>(hParent), currentPort, currentToken, currentDeviceSyncSec, currentBatteryRefreshSec, newPort, newToken, newDeviceSyncSec, newBatteryRefreshSec);
    if (!accepted)
        return OR_OPTION_UNCHANGED;

    bool changed = (newPort != currentPort) || (newToken != currentToken) || (newDeviceSyncSec != currentDeviceSyncSec) || (newBatteryRefreshSec != currentBatteryRefreshSec);
    if (!changed)
        return OR_OPTION_UNCHANGED;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_apiPort = newPort;
        m_apiToken = newToken;
        m_authFailCount = 0;
        m_pluginDisabled = false;
        m_deviceSyncIntervalMs = newDeviceSyncSec * 1000;
        m_batteryRefreshIntervalMs = newBatteryRefreshSec * 1000;
        m_lastDeviceSyncTick = 0;
        m_lastBatteryRefreshTick = 0;
    }
    SaveConfig();
    FetchAndUpdate(true);
    unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastDeviceSyncTick = now;
        m_lastBatteryRefreshTick = now;
    }
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
    wchar_t tokenText[512] = {};
    GetPrivateProfileStringW(L"auth", L"token", L"", tokenText, 512, configPath.c_str());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_apiToken = tokenText;
    }
    if (port >= 1 && port <= 65535)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_apiPort = port;
    }

    int fallbackDeviceSyncSec = 5;
    int fallbackBatteryRefreshSec = 2;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fallbackDeviceSyncSec = m_deviceSyncIntervalMs / 1000;
        fallbackBatteryRefreshSec = m_batteryRefreshIntervalMs / 1000;
    }

    int deviceSyncSec = GetPrivateProfileIntW(L"timing", L"device_sync_sec", fallbackDeviceSyncSec, configPath.c_str());
    int batteryRefreshSec = GetPrivateProfileIntW(L"timing", L"battery_refresh_sec", fallbackBatteryRefreshSec, configPath.c_str());
    if (deviceSyncSec < 1) deviceSyncSec = 1;
    if (deviceSyncSec > 3600) deviceSyncSec = 3600;
    if (batteryRefreshSec < 1) batteryRefreshSec = 1;
    if (batteryRefreshSec > 3600) batteryRefreshSec = 3600;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deviceSyncIntervalMs = deviceSyncSec * 1000;
        m_batteryRefreshIntervalMs = batteryRefreshSec * 1000;
    }
}

void BatteryPlugin::SaveConfig()
{
    std::wstring configPath = GetConfigPath();
    if (configPath.empty())
        return;

    int port = 18080;
    std::wstring token;
    int deviceSyncSec = 5;
    int batteryRefreshSec = 2;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
        token = m_apiToken;
        deviceSyncSec = m_deviceSyncIntervalMs / 1000;
        batteryRefreshSec = m_batteryRefreshIntervalMs / 1000;
    }
    wchar_t text[16] = {};
    wsprintfW(text, L"%d", port);
    WritePrivateProfileStringW(L"network", L"port", text, configPath.c_str());
    WritePrivateProfileStringW(L"auth", L"token", token.c_str(), configPath.c_str());

    wchar_t syncText[16] = {};
    wchar_t refreshText[16] = {};
    wsprintfW(syncText, L"%d", deviceSyncSec);
    wsprintfW(refreshText, L"%d", batteryRefreshSec);
    WritePrivateProfileStringW(L"timing", L"device_sync_sec", syncText, configPath.c_str());
    WritePrivateProfileStringW(L"timing", L"battery_refresh_sec", refreshText, configPath.c_str());
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
