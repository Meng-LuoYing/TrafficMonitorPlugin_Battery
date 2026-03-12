#include "pch.h"
#include "BatteryPlugin.h"
#include "HttpClient.h"
#include "JsonParser.h"
#include <cwchar>
#include <shellapi.h>
#include <sstream>
#include <algorithm>
#include <commctrl.h>

// 选项对话框相关的匿名命名空间
namespace
{
    // 选项对话框状态结构
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
        HWND hDeviceGroup = nullptr;
        HWND hPortLabel = nullptr;
        HWND hTokenLabel = nullptr;
        HWND hDeviceSyncLabel = nullptr;
        HWND hBatteryRefreshLabel = nullptr;
        HWND hOkButton = nullptr;
        HWND hCancelButton = nullptr;
        HWND hApplyButton = nullptr;
        HWND hRefreshButton = nullptr;
        HWND hMoveUpButton = nullptr;
        HWND hMoveDownButton = nullptr;
        HFONT hFont = nullptr;
        bool applied = false;
        HWND hDeviceList = nullptr;
        std::vector<std::wstring> deviceIds;
        BatteryPlugin* plugin = nullptr;
        int scrollPos = 0;      // 当前垂直滚动位置（像素）
        int totalContentH = 0;  // 总虚拟内容高度（像素）
        bool bCheckingLimits = false; // Prevents recursive checkbox checks
    };

    // 控件 ID 常量定义
    constexpr int ID_PORT_EDIT = 1001;
    constexpr int ID_TOKEN_EDIT = 1002;
    constexpr int ID_DEVICE_SYNC_EDIT = 1003;
    constexpr int ID_BATTERY_REFRESH_EDIT = 1004;
    constexpr int ID_APPLY_BUTTON = 1005;
    constexpr int ID_TOKEN_PROMPT_EDIT = 1101;
    constexpr int ID_REFRESH_BUTTON = 1102;
    constexpr int ID_MOVE_UP_BUTTON = 1103;
    constexpr int ID_MOVE_DOWN_BUTTON = 1104;
    constexpr int ID_DEVICE_CHECKBOX_BASE = 2000;
    constexpr int ID_DEVICE_LIST = 2001;
    constexpr int DIALOG_WIDTH = 380;
    constexpr int DIALOG_HEIGHT = 560;

    // 尝试解析整数文本
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

    // Subclass proc for hDeviceGroup:
    // (1) Forwards WM_COMMAND from child controls (checkboxes, refresh button) to the main dialog.
    // (2) Returns a white brush for WM_CTLCOLORBTN so checkboxes have a clean background.
    LRESULT CALLBACK DeviceGroupSubclassProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
    {
        switch (msg)
        {
        case WM_COMMAND:
            // Forward to grandparent (PortDialogProc)
            SendMessageW(GetParent(hWnd), WM_COMMAND, wParam, lParam);
            return 0;
        case WM_CTLCOLORBTN:
        {
            // Remove the grey background from child checkboxes/buttons
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_CTLCOLORSTATIC:
        {
            // Remove the grey background from child checkboxes (they also send STATIC messages)
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_ERASEBKGND:
        {
            // Windows GroupBox doesn't erase its background properly when resized larger,
            // leaving behind old borders. We manually erase it here with the window color.
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
            return 1; // Return non-zero to indicate we handled erasure
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, DeviceGroupSubclassProc, uIdSubclass);
            break;
        }
        return DefSubclassProc(hWnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK DeviceListSubclassProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
    {
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
        {
            int checkedCount = 0;
            int count = ListView_GetItemCount(hWnd);
            for (int i = 0; i < count; ++i)
                if (ListView_GetCheckState(hWnd, i))
                    checkedCount++;

            if (checkedCount >= 4)
            {
                LVHITTESTINFO hti = {};
                hti.pt.x = (short)LOWORD(lParam);
                hti.pt.y = (short)HIWORD(lParam);
                ListView_HitTest(hWnd, &hti);
                
                if (hti.flags & LVHT_ONITEMSTATEICON)
                {
                    if (hti.iItem >= 0 && hti.iItem < count) {
                        if (!ListView_GetCheckState(hWnd, hti.iItem)) {
                            return 0; // block click
                        }
                    }
                }
            }
        }
        else if (msg == WM_KEYDOWN && wParam == VK_SPACE)
        {
            int checkedCount = 0;
            int count = ListView_GetItemCount(hWnd);
            for (int i = 0; i < count; ++i)
                if (ListView_GetCheckState(hWnd, i))
                    checkedCount++;

            if (checkedCount >= 4)
            {
                int targetItem = ListView_GetNextItem(hWnd, -1, LVNI_FOCUSED);
                if (targetItem >= 0 && targetItem < count) {
                    if (!ListView_GetCheckState(hWnd, targetItem)) {
                        return 0; // block spacebar
                    }
                }
            }
        }
        else if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hWnd, DeviceListSubclassProc, uIdSubclass);
        }
        return DefSubclassProc(hWnd, msg, wParam, lParam);
    }

    void RefreshDeviceList(OptionsDialogState* state, HWND hDialog)
    {
        if (!state || !state->plugin || !hDialog || !state->hDeviceList) return;

        ListView_DeleteAllItems(state->hDeviceList);
        state->deviceIds.clear();

        // Get available devices and selected devices
        auto devices = state->plugin->GetAvailableDevices();
        auto selectedDevices = state->plugin->GetSelectedDevices();
        
        int lvIndex = 0;
        
        // 显示已选中的设备
        for (const auto& selectedId : selectedDevices)
        {
            for (const auto& dev : devices)
            {
                if (dev.id == selectedId)
                {
                    state->deviceIds.push_back(dev.id);
                    LVITEMW lvi = {};
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = lvIndex;
                    lvi.pszText = const_cast<LPWSTR>(dev.name.c_str());
                    ListView_InsertItem(state->hDeviceList, &lvi);
                    ListView_SetCheckState(state->hDeviceList, lvIndex, TRUE);
                    lvIndex++;
                    break;
                }
            }
        }
        
        // 显示未选中的设备
        for (const auto& dev : devices)
        {
            // 跳过已选中的设备
            bool isSelected = false;
            for (const auto& selectedId : selectedDevices)
            {
                if (dev.id == selectedId)
                {
                    isSelected = true;
                    break;
                }
            }
            
            if (!isSelected)
            {
                state->deviceIds.push_back(dev.id);
                LVITEMW lvi = {};
                lvi.mask = LVIF_TEXT;
                lvi.iItem = lvIndex;
                lvi.pszText = const_cast<LPWSTR>(dev.name.c_str());
                ListView_InsertItem(state->hDeviceList, &lvi);
                ListView_SetCheckState(state->hDeviceList, lvIndex, FALSE);
                lvIndex++;
            }
        }
    }

    // Layout all controls accounting for current scroll position.
    // Content controls are shifted by -scrollPos; button bar stays fixed.
    void LayoutAndScroll(OptionsDialogState* state, HWND hWnd)
    {
        if (!state || !state->hNetworkGroup) return;
        RECT rc;
        GetClientRect(hWnd, &rc);
        int clientW = rc.right;
        int clientH = rc.bottom;

        const int margin    = 16;
        const int buttonH   = 28;
        const int buttonGap = 8;
        const int buttonBarH = buttonH + 28; // reserved at bottom
        int contentAreaH = clientH - buttonBarH;
        if (contentAreaH < 50) contentAreaH = 50;
        int buttonY = clientH - buttonH - 14;
        int groupW  = clientW - margin * 2;
        if (groupW < 200) groupW = 200;

        // Logical (unscrolled) top positions
        const int networkVirtTop = 16;
        const int networkH       = 122;
        const int timingVirtTop  = networkVirtTop + networkH + 10; // 148
        const int timingH        = 124;
        const int deviceVirtTop  = timingVirtTop + timingH + 10;   // 282

        // Device group height
        int deviceH = 150;

        state->totalContentH = deviceVirtTop + deviceH + 16;

        // Clamp scroll position - 确保设备移除后滚动位置正确
        int maxScroll = state->totalContentH - contentAreaH;
        if (maxScroll < 0) maxScroll = 0;
        if (state->scrollPos < 0) state->scrollPos = 0;
        if (state->scrollPos > maxScroll) state->scrollPos = maxScroll;
        
        // 如果内容高度小于可视区域，重置滚动位置到顶部
        if (state->totalContentH <= contentAreaH)
        {
            state->scrollPos = 0;
        }

        // Update main window scrollbar
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin   = 0;
        si.nMax   = state->totalContentH;
        si.nPage  = (UINT)contentAreaH;
        si.nPos   = state->scrollPos;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);

        int sp = state->scrollPos;
        int labelLeft = margin + 16;
        int valueLeft = 230;
        int valueW    = groupW - (valueLeft - margin) - 20;
        if (valueW < 80) valueW = 80;

        // Network group + controls
        SetWindowPos(state->hNetworkGroup,      nullptr, margin, networkVirtTop - sp, groupW, networkH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hPortLabel,         nullptr, labelLeft, networkVirtTop + 26 - sp, 190, 20,   SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hPortEdit,          nullptr, valueLeft, networkVirtTop + 22 - sp, valueW, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hTokenLabel,        nullptr, labelLeft, networkVirtTop + 70 - sp, 190, 20,   SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hTokenEdit,         nullptr, valueLeft, networkVirtTop + 66 - sp, valueW, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        // Timing group + controls
        SetWindowPos(state->hTimingGroup,        nullptr, margin, timingVirtTop - sp, groupW, timingH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hDeviceSyncLabel,    nullptr, labelLeft, timingVirtTop + 26 - sp, 190, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hDeviceSyncEdit,     nullptr, valueLeft, timingVirtTop + 22 - sp, valueW, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hBatteryRefreshLabel,nullptr, labelLeft, timingVirtTop + 70 - sp, 190, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hBatteryRefreshEdit, nullptr, valueLeft, timingVirtTop + 66 - sp, valueW, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        // Device group: resize with SWP_FRAMECHANGED so the groupbox border is redrawn correctly
        SetWindowPos(state->hDeviceGroup, nullptr, margin, deviceVirtTop - sp, groupW, deviceH,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        // Refresh button: top-right inside device group
        if (state->hRefreshButton)
            SetWindowPos(state->hRefreshButton, nullptr, groupW - 80, 20, 64, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        // Move up button
        if (state->hMoveUpButton)
            SetWindowPos(state->hMoveUpButton, nullptr, groupW - 80, 56, 64, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        // Move down button
        if (state->hMoveDownButton)
            SetWindowPos(state->hMoveDownButton, nullptr, groupW - 80, 92, 64, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        // Resize List View
        if (state->hDeviceList)
        {
            int listW = groupW - 100;
            SetWindowPos(state->hDeviceList, nullptr, 16, 20, listW, deviceH - 32, SWP_NOZORDER | SWP_NOACTIVATE);
            ListView_SetColumnWidth(state->hDeviceList, 0, listW - 25);
        }

        // Fixed button bar
        int right = clientW - margin;
        SetWindowPos(state->hCancelButton, nullptr, right - 80,                         buttonY, 80, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hOkButton,     nullptr, right - 80 * 2 - buttonGap,        buttonY, 80, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->hApplyButton,  nullptr, right - 80 * 3 - buttonGap * 2,    buttonY, 80, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);

        // Full synchronous repaint: erase background, redraw frames and all children
        RedrawWindow(hWnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
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
                state->hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (!state->hFont)
                state->hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            // All controls are direct children of hWnd (no intermediate STATIC container).
            // Groups
            state->hNetworkGroup = CreateWindowW(L"BUTTON", L"\u7F51\u7EDC\u8BBE\u7F6E",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 16, 16, 500, 122, hWnd, nullptr, nullptr, nullptr);
            state->hTimingGroup = CreateWindowW(L"BUTTON", L"\u5237\u65B0\u8BBE\u7F6E",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 16, 148, 500, 124, hWnd, nullptr, nullptr, nullptr);
            state->hDeviceGroup = CreateWindowW(L"BUTTON", L"\u8BBE\u5907\u5217\u8868\uFF08\u6700\u591A\u53EA\u80FD\u9009\u62E94\u53F0\u8BBE\u5907\uFF09",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX | WS_CLIPCHILDREN, 16, 282, 500, 100, hWnd, nullptr, nullptr, nullptr);

            // Labels
            state->hPortLabel = CreateWindowW(L"STATIC", L"API \u7AEF\u53E3\uFF1A",
                WS_CHILD | WS_VISIBLE, 32, 46, 130, 20, hWnd, nullptr, nullptr, nullptr);
            state->hTokenLabel = CreateWindowW(L"STATIC", L"Token\uFF1A",
                WS_CHILD | WS_VISIBLE, 32, 90, 130, 20, hWnd, nullptr, nullptr, nullptr);
            state->hDeviceSyncLabel = CreateWindowW(L"STATIC", L"\u8BBE\u5907\u53D8\u52A8\u68C0\u6D4B\u95F4\u9694\uFF08\u79D2\uFF0C1-3600\uFF09\uFF1A",
                WS_CHILD | WS_VISIBLE, 32, 178, 260, 20, hWnd, nullptr, nullptr, nullptr);
            state->hBatteryRefreshLabel = CreateWindowW(L"STATIC", L"\u7535\u91CF\u5237\u65B0\u95F4\u9694\uFF08\u79D2\uFF0C1-3600\uFF09\uFF1A",
                WS_CHILD | WS_VISIBLE, 32, 222, 260, 20, hWnd, nullptr, nullptr, nullptr);

            // Edits
            wchar_t portText[16] = {};
            wchar_t syncText[16] = {};
            wchar_t refreshText[16] = {};
            wsprintfW(portText, L"%d", state->currentPort);
            wsprintfW(syncText, L"%d", state->currentDeviceSyncSec);
            wsprintfW(refreshText, L"%d", state->currentBatteryRefreshSec);

            state->hPortEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", portText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 42, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_PORT_EDIT, nullptr, nullptr);
            state->hTokenEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->currentToken.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 86, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_TOKEN_EDIT, nullptr, nullptr);
            state->hDeviceSyncEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", syncText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 174, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_DEVICE_SYNC_EDIT, nullptr, nullptr);
            state->hBatteryRefreshEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", refreshText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 218, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_BATTERY_REFRESH_EDIT, nullptr, nullptr);
            SendMessageW(state->hPortEdit, EM_SETLIMITTEXT, 5, 0);
            SendMessageW(state->hTokenEdit, EM_SETLIMITTEXT, 256, 0);
            SendMessageW(state->hDeviceSyncEdit, EM_SETLIMITTEXT, 4, 0);
            SendMessageW(state->hBatteryRefreshEdit, EM_SETLIMITTEXT, 4, 0);

            // List View
            state->hDeviceList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                16, 30, 400, 100,
                state->hDeviceGroup, (HMENU)(INT_PTR)ID_DEVICE_LIST, nullptr, nullptr);
            ListView_SetExtendedListViewStyle(state->hDeviceList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            
            LVCOLUMNW lvc = {};
            lvc.mask = LVCF_WIDTH;
            lvc.cx = 380; // approximate initial width
            ListView_InsertColumn(state->hDeviceList, 0, &lvc);

            // Refresh button
            state->hRefreshButton = CreateWindowW(L"BUTTON", L"\u5237\u65B0",
                WS_CHILD | WS_VISIBLE,
                420, 20, 64, 26,
                state->hDeviceGroup, (HMENU)(INT_PTR)ID_REFRESH_BUTTON, nullptr, nullptr);

            // Move up button
            state->hMoveUpButton = CreateWindowW(L"BUTTON", L"\u4E0A\u79FB", // 上移
                WS_CHILD | WS_VISIBLE,
                420, 56, 64, 26,
                state->hDeviceGroup, (HMENU)(INT_PTR)ID_MOVE_UP_BUTTON, nullptr, nullptr);

            // Move down button  
            state->hMoveDownButton = CreateWindowW(L"BUTTON", L"\u4E0B\u79FB", // 下移
                WS_CHILD | WS_VISIBLE,
                420, 92, 64, 26,
                state->hDeviceGroup, (HMENU)(INT_PTR)ID_MOVE_DOWN_BUTTON, nullptr, nullptr);

            // Bottom buttons (fixed, not scrolled)
            state->hApplyButton = CreateWindowW(L"BUTTON", L"\u5E94\u7528", WS_CHILD | WS_VISIBLE,
                226, DIALOG_HEIGHT - 40, 80, 28, hWnd, (HMENU)(INT_PTR)ID_APPLY_BUTTON, nullptr, nullptr);
            state->hOkButton = CreateWindowW(L"BUTTON", L"\u786E\u5B9A", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                314, DIALOG_HEIGHT - 40, 80, 28, hWnd, (HMENU)(INT_PTR)IDOK, nullptr, nullptr);
            state->hCancelButton = CreateWindowW(L"BUTTON", L"\u53D6\u6D88", WS_CHILD | WS_VISIBLE,
                402, DIALOG_HEIGHT - 40, 80, 28, hWnd, (HMENU)(INT_PTR)IDCANCEL, nullptr, nullptr);

            // Apply font to all controls
            auto setFont = [&](HWND h) { if (h && state->hFont) SendMessageW(h, WM_SETFONT, (WPARAM)state->hFont, TRUE); };
            setFont(state->hNetworkGroup); setFont(state->hTimingGroup); setFont(state->hDeviceGroup);
            setFont(state->hPortLabel);    setFont(state->hTokenLabel);
            setFont(state->hDeviceSyncLabel); setFont(state->hBatteryRefreshLabel);
            setFont(state->hPortEdit);     setFont(state->hTokenEdit);
            setFont(state->hDeviceSyncEdit); setFont(state->hBatteryRefreshEdit);
            setFont(state->hDeviceList);
            setFont(state->hRefreshButton);
            setFont(state->hMoveUpButton); setFont(state->hMoveDownButton);
            setFont(state->hOkButton); setFont(state->hCancelButton); setFont(state->hApplyButton);

            // Load device list (this also triggers WM_SIZE -> LayoutAndScroll)
            RefreshDeviceList(state, hWnd);

            // Subclass hDeviceGroup to forward WM_COMMAND to hWnd and provide white background
            SetWindowSubclass(state->hDeviceGroup, DeviceGroupSubclassProc, 1, 0);
            
            // Subclass hDeviceList to intercept clicks
            SetWindowSubclass(state->hDeviceList, DeviceListSubclassProc, 1, 0);
            return 0;
        }
        case WM_SIZE:
            if (state) LayoutAndScroll(state, hWnd);
            return 0;
        case WM_SHOWWINDOW:
            if (wParam && state && state->hPortEdit)
            {
                LayoutAndScroll(state, hWnd);
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
        case WM_NOTIFY:
        {
            auto pnm = reinterpret_cast<LPNMHDR>(lParam);
            if (!state || !state->plugin || !state->hDeviceList) return 0;

            if (pnm->hwndFrom == state->hDeviceList && pnm->code == LVN_ITEMCHANGED)
            {
                auto pnmv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if ((pnmv->uChanged & LVIF_STATE) && 
                    (pnmv->uNewState & LVIS_STATEIMAGEMASK) != (pnmv->uOldState & LVIS_STATEIMAGEMASK))
                {
                    // Forces full redraw to update the gray text over items
                    InvalidateRect(state->hDeviceList, nullptr, FALSE);
                }
                return 0;
            }

            if (pnm->hwndFrom == state->hDeviceList && pnm->code == NM_CUSTOMDRAW)
            {
                auto pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                switch (pLVCD->nmcd.dwDrawStage)
                {
                case CDDS_PREPAINT:
                    SetWindowLongPtr(hWnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                {
                    int checkedCount = 0;
                    int count = ListView_GetItemCount(state->hDeviceList);
                    for (int i = 0; i < count; ++i)
                        if (ListView_GetCheckState(state->hDeviceList, i))
                            checkedCount++;

                    bool isChecked = ListView_GetCheckState(state->hDeviceList, pLVCD->nmcd.dwItemSpec);
                    if (!isChecked && checkedCount >= 4)
                    {
                        // Dim the text to show it's disabled
                        pLVCD->clrText = GetSysColor(COLOR_GRAYTEXT);
                    }
                    SetWindowLongPtr(hWnd, DWLP_MSGRESULT, CDRF_DODEFAULT);
                    return CDRF_DODEFAULT;
                }
                }
                return 0;
            }
            return 0;
        }
        case WM_COMMAND:
        {
            const int cmd = LOWORD(wParam);
            if (cmd == ID_REFRESH_BUTTON && state && state->plugin)
            {
                // Force a live API fetch first, then update the checkbox list
                state->plugin->RefreshDevicesNow();
                RefreshDeviceList(state, hWnd);
                return 0;
            }
            
            // Handle move up button
            if (cmd == ID_MOVE_UP_BUTTON && state && state->plugin && state->hDeviceList)
            {
                int curSel = ListView_GetNextItem(state->hDeviceList, -1, LVNI_SELECTED);
                if (curSel > 0)
                {
                    // Swap with the item above
                    wchar_t itemText[1024];
                    ListView_GetItemText(state->hDeviceList, curSel, 0, itemText, 1024);
                    bool isChecked = ListView_GetCheckState(state->hDeviceList, curSel);
                    std::wstring id = state->deviceIds[curSel];

                    wchar_t aboveText[1024];
                    ListView_GetItemText(state->hDeviceList, curSel - 1, 0, aboveText, 1024);
                    bool aboveChecked = ListView_GetCheckState(state->hDeviceList, curSel - 1);
                    std::wstring aboveId = state->deviceIds[curSel - 1];

                    // Swap texts and states in list view
                    ListView_SetItemText(state->hDeviceList, curSel, 0, aboveText);
                    ListView_SetCheckState(state->hDeviceList, curSel, aboveChecked);
                    state->deviceIds[curSel] = aboveId;

                    ListView_SetItemText(state->hDeviceList, curSel - 1, 0, itemText);
                    ListView_SetCheckState(state->hDeviceList, curSel - 1, isChecked);
                    state->deviceIds[curSel - 1] = id;

                    ListView_SetItemState(state->hDeviceList, curSel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
                return 0;
            }
            
            // Handle move down button
            if (cmd == ID_MOVE_DOWN_BUTTON && state && state->plugin && state->hDeviceList)
            {
                int curSel = ListView_GetNextItem(state->hDeviceList, -1, LVNI_SELECTED);
                int count = ListView_GetItemCount(state->hDeviceList);
                if (curSel >= 0 && curSel < count - 1)
                {
                    // Swap with the item below
                    wchar_t itemText[1024];
                    ListView_GetItemText(state->hDeviceList, curSel, 0, itemText, 1024);
                    bool isChecked = ListView_GetCheckState(state->hDeviceList, curSel);
                    std::wstring id = state->deviceIds[curSel];

                    wchar_t belowText[1024];
                    ListView_GetItemText(state->hDeviceList, curSel + 1, 0, belowText, 1024);
                    bool belowChecked = ListView_GetCheckState(state->hDeviceList, curSel + 1);
                    std::wstring belowId = state->deviceIds[curSel + 1];

                    // Swap texts and states in list view
                    ListView_SetItemText(state->hDeviceList, curSel, 0, belowText);
                    ListView_SetCheckState(state->hDeviceList, curSel, belowChecked);
                    state->deviceIds[curSel] = belowId;

                    ListView_SetItemText(state->hDeviceList, curSel + 1, 0, itemText);
                    ListView_SetCheckState(state->hDeviceList, curSel + 1, isChecked);
                    state->deviceIds[curSel + 1] = id;

                    ListView_SetItemState(state->hDeviceList, curSel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
                return 0;
            }
            
            if ((cmd == IDOK || cmd == ID_APPLY_BUTTON) && state && state->hPortEdit && state->hTokenEdit && state->hDeviceSyncEdit && state->hBatteryRefreshEdit)
            {
                // Apply device selections and ordering based on ListView state
                if (state->hDeviceList && state->plugin)
                {
                    // For Apply/OK, we clear the plugin selected devices, and iterate the list from top to bottom.
                    // If checked, we add the corresponding device ID.
                    int count = ListView_GetItemCount(state->hDeviceList);
                    std::vector<std::pair<std::wstring, bool>> selectionsToApply;
                    for (int i = 0; i < count; ++i)
                    {
                        bool isChecked = ListView_GetCheckState(state->hDeviceList, i);
                        selectionsToApply.push_back({ state->deviceIds[i], isChecked });
                    }

                    // Loop and clear all first, then set in order
                    state->plugin->GetSelectedDevices(); // ensure plugin loads, not really necessary
                    auto existing = state->plugin->GetAvailableDevices();
                    for (const auto& dev : existing) {
                        state->plugin->SetDeviceSelection(dev.id, false);
                    }
                    for (const auto& sel : selectionsToApply) {
                        if (sel.second) {
                            state->plugin->SetDeviceSelection(sel.first, true);
                        }
                    }
                }

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
        case WM_APP + 100:
        {
            // 延迟重绘设备组，避免在控件创建过程中出现横线
            if (state && state->hDeviceGroup)
            {
                InvalidateRect(state->hDeviceGroup, nullptr, TRUE);
                UpdateWindow(state->hDeviceGroup);
            }
            return 0;
        }
        case WM_VSCROLL:
        {
            // Handles clicks on the main window's own integrated scrollbar (lParam == 0)
            if (!state) break;
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hWnd, SB_VERT, &si);
            int yPos = si.nPos;
            switch (LOWORD(wParam))
            {
            case SB_LINEUP:    yPos -= 20; break;
            case SB_LINEDOWN:  yPos += 20; break;
            case SB_PAGEUP:    yPos -= (int)si.nPage; break;
            case SB_PAGEDOWN:  yPos += (int)si.nPage; break;
            case SB_THUMBTRACK: yPos = HIWORD(wParam); break;
            case SB_TOP:    yPos = si.nMin; break;
            case SB_BOTTOM: yPos = (int)(si.nMax - (int)si.nPage); break;
            }
            if (yPos < si.nMin) yPos = si.nMin;
            if (yPos > (int)(si.nMax - (int)si.nPage)) yPos = (int)(si.nMax - (int)si.nPage);
            state->scrollPos = yPos;
            LayoutAndScroll(state, hWnd);
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            if (!state) break;
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            state->scrollPos += (zDelta > 0) ? -60 : 60;
            LayoutAndScroll(state, hWnd);
            return 0;
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
            // Clean up list view
            if (state && state->hDeviceList)
            {
                DestroyWindow(state->hDeviceList);
                state->hDeviceList = nullptr;
            }
            if (state)
            {
                state->deviceIds.clear();
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
        state.plugin = &BatteryPlugin::Instance();

        if (ownerWindow) EnableWindow(ownerWindow, FALSE);
        HWND hWnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            CLASS_NAME,
            L"\u8BBE\u5907\u7535\u91CF \u63D2\u4EF6\u9009\u9879",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VSCROLL,
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
        if (m_displayItem)
            m_displayItem->SetOffline();
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
    
    bool needsRebuild = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_authFailCount = 0;
        m_pluginDisabled = false;
        m_availableDevices = devices;
        
        if (m_autoSelectFirstDevices && !devices.empty()) {
            m_autoSelectFirstDevices = false;
            for (const auto& dev : devices) {
                if (m_selectedDevices.size() >= 4) break;
                m_selectedDevices.push_back(dev.id);
            }
            needsRebuild = true;
        }
    }
    
    if (needsRebuild) {
        RebuildItems();
        SaveConfig();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    // Convert selected set to ordered vector
    std::vector<std::wstring> orderedIds(m_selectedDevices.begin(), m_selectedDevices.end());

    // Update each m_items up to 4 slots
    for (size_t i = 0; i < 4 && i < m_items.size(); ++i) {
        if (i < orderedIds.size()) {
            const std::wstring& sid = orderedIds[i];
            bool found = false;
            for (const auto& dev : devices) {
                if (dev.id == sid) {
                    m_items[i]->Update(dev);
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_items[i]->SetOffline();
            }
        } else {
            // Unselected slots remain as placeholders
            m_items[i]->InitWithId(L"");
            m_items[i]->SetOffline();
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
    
    // Track if device selection changed to force restart
    std::vector<std::wstring> oldSelected;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oldSelected = m_selectedDevices;
    }

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
    
    // Check if the actual selected device set changed
    bool devicesChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        devicesChanged = (oldSelected != m_selectedDevices);
    }
    
    SaveConfig();
    RebuildItems(); // Rebuild items locally so new devices are allocated
    FetchAndUpdate(true);
    unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastDeviceSyncTick = now;
        m_lastBatteryRefreshTick = now;
    }
    
    // If the number of items changes or item identities change, TM needs a restart to pick up new GetItem() layout
    if (devicesChanged) {
        RestartTrafficMonitor();
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

    // Load device selections
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_selectedDevices.clear();
    }
    
    wchar_t selectedDevicesText[2048] = {};
    GetPrivateProfileStringW(L"devices", L"selected", L"__UNINITIALIZED__", selectedDevicesText, 2048, configPath.c_str());
    
    if (wcscmp(selectedDevicesText, L"__UNINITIALIZED__") == 0)
    {
        // First run, no selection saved. Auto select later.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_autoSelectFirstDevices = true;
    }
    else if (selectedDevicesText[0] != L'\0')
    {
        std::wstringstream ss(selectedDevicesText);
        std::wstring deviceId;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_selectedDevices.clear();
        while (std::getline(ss, deviceId, L','))
        {
            if (!deviceId.empty())
            {
                m_selectedDevices.push_back(deviceId);
            }
        }
    }
    
    // Config loaded, recreate the item list so GetItem() can report the correct count
    RebuildItems();
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
    std::vector<std::wstring> selectedDevices;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
        token = m_apiToken;
        deviceSyncSec = m_deviceSyncIntervalMs / 1000;
        batteryRefreshSec = m_batteryRefreshIntervalMs / 1000;
        selectedDevices = m_selectedDevices;
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

    // Save device selections
    std::wstring selectedDevicesText;
    for (const auto& deviceId : selectedDevices)
    {
        if (!selectedDevicesText.empty())
            selectedDevicesText += L",";
        selectedDevicesText += deviceId;
    }
    WritePrivateProfileStringW(L"devices", L"selected", selectedDevicesText.c_str(), configPath.c_str());
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

void BatteryPlugin::RebuildItems()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // We maintain a stable order by iterating the set up to 4 items
    std::vector<std::wstring> orderedIds;
    for (const auto& id : m_selectedDevices) {
        if (orderedIds.size() >= 4) break;
        orderedIds.push_back(id);
    }
    
    // Always maintain exactly 4 items
    while (m_items.size() > 4) {
        m_items.pop_back();
    }
    while (m_items.size() < 4) {
        auto item = std::make_unique<BatteryItem>();
        m_items.push_back(std::move(item));
    }
    
    // Bind selected devices to slots, and clear the remaining slots
    for (size_t i = 0; i < 4; ++i) {
        m_items[i]->SetIndex((int)i);
        if (i < orderedIds.size()) {
            m_items[i]->InitWithId(orderedIds[i]); 
        } else {
            // Clear unselected slots
            m_items[i]->InitWithId(L"");
            m_items[i]->SetOffline(); // Forces empty display state
        }
    }
}

std::vector<DeviceBattery> BatteryPlugin::GetAvailableDevices() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_availableDevices;
}

std::vector<std::wstring> BatteryPlugin::GetSelectedDevices() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedDevices;
}

void BatteryPlugin::SetDeviceSelection(const std::wstring& deviceId, bool selected)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_selectedDevices.begin(), m_selectedDevices.end(), deviceId);
    if (selected && it == m_selectedDevices.end())
    {
        // 如果设备未被选中，则添加到末尾
        m_selectedDevices.push_back(deviceId);
    }
    else if (!selected && it != m_selectedDevices.end())
    {
        // 如果设备已被选中，则移除
        m_selectedDevices.erase(it);
    }
}

bool BatteryPlugin::IsDeviceSelected(const std::wstring& deviceId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::find(m_selectedDevices.begin(), m_selectedDevices.end(), deviceId) != m_selectedDevices.end();
}

void BatteryPlugin::RefreshDevicesNow()
{
    // Makes a blocking HTTP request to get the latest device list.
    // Called from the dialog UI thread when the user clicks the Refresh button.
    FetchAndUpdate(true);
}

// 设备顺序上移
void BatteryPlugin::MoveDeviceUp(const std::wstring& deviceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_selectedDevices.begin(), m_selectedDevices.end(), deviceId);
    if (it == m_selectedDevices.end())
    {
        // 设备不存在于选中列表中
        return;
    }
    
    if (it == m_selectedDevices.begin())
    {
        // 设备已经是第一个，无法上移
        return;
    }
    
    // 与前一个设备交换位置
    std::iter_swap(it, it - 1);
    RebuildItems();
}

// 设备顺序下移
void BatteryPlugin::MoveDeviceDown(const std::wstring& deviceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_selectedDevices.begin(), m_selectedDevices.end(), deviceId);
    if (it == m_selectedDevices.end())
    {
        // 设备不存在于选中列表中
        return;
    }
    
    if (it + 1 == m_selectedDevices.end())
    {
        // 设备已经是最后一个，无法下移
        return;
    }
    
    // 与后一个设备交换位置
    std::iter_swap(it, it + 1);
    RebuildItems();
}

ITMPlugin* TMPluginGetInstance()
{
    return &BatteryPlugin::Instance();
}
