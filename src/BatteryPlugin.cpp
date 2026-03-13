#include "pch.h"
#include "BatteryPlugin.h"
#include "HttpClient.h"
#include "JsonParser.h"
#include <cwchar>
#include <shellapi.h>
#include <sstream>
#include <algorithm>
#include <commctrl.h>
#include <thread>

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
        int currentRefreshSec = 2;
        int resultRefreshSec = 2;
        bool accepted = false;
        HWND hPortEdit = nullptr;
        HWND hTokenEdit = nullptr;
        HWND hRefreshEdit = nullptr;
        HWND hNetworkGroup = nullptr;
        HWND hTimingGroup = nullptr;
        HWND hDeviceGroup = nullptr;
        HWND hPortLabel = nullptr;
        HWND hTokenLabel = nullptr;
        HWND hRefreshLabel = nullptr;
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
    constexpr int DIALOG_WIDTH = 360;
    constexpr int DIALOG_HEIGHT = 480;

    // 尝试解析整数文本
    bool TryParseInteger(const wchar_t* text, int minValue, int maxValue, int& out)
    {
        if (text == nullptr) return false;
        // Trim leading spaces
        while (*text == L' ') text++;
        if (text[0] == L'\0') return false;
        
        wchar_t* end = nullptr;
        long value = wcstol(text, &end, 10);
        if (end == text) return false;
        
        // Trim trailing spaces
        while (*end == L' ') end++;
        
        if (*end != L'\0' || value < minValue || value > maxValue)
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
        case WM_NOTIFY:
            // Forward to grandparent (PortDialogProc)
            return SendMessageW(GetParent(hWnd), msg, wParam, lParam);
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

        state->bCheckingLimits = true;
        ListView_DeleteAllItems(state->hDeviceList);
        state->deviceIds.clear();

        // Get refresh devices (from refresh button click) and selected devices
        auto devices = state->plugin->GetRefreshDevices(); // 使用刷新设备列表
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
        
        state->bCheckingLimits = false;
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
        const int networkH       = 106;
        const int timingVirtTop  = networkVirtTop + networkH + 10;
        const int timingH        = 66;
        const int deviceVirtTop  = timingVirtTop + timingH + 10;

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

        // Common flags for moving children
        const UINT moveFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;

        // Network group + controls
        SetWindowPos(state->hNetworkGroup,      nullptr, margin, networkVirtTop - sp, groupW, networkH, moveFlags);
        SetWindowPos(state->hPortLabel,         nullptr, labelLeft, networkVirtTop + 30 - sp, 190, 20,   moveFlags);
        SetWindowPos(state->hPortEdit,          nullptr, valueLeft, networkVirtTop + 28 - sp, valueW, 24, moveFlags);
        SetWindowPos(state->hTokenLabel,        nullptr, labelLeft, networkVirtTop + 66 - sp, 190, 20,   moveFlags);
        SetWindowPos(state->hTokenEdit,         nullptr, valueLeft, networkVirtTop + 64 - sp, valueW, 24, moveFlags);

        // Timing group + controls
        SetWindowPos(state->hTimingGroup,        nullptr, margin, timingVirtTop - sp, groupW, timingH, moveFlags);
        SetWindowPos(state->hRefreshLabel,       nullptr, labelLeft, timingVirtTop + 30 - sp, 260, 20, moveFlags);
        SetWindowPos(state->hRefreshEdit,        nullptr, valueLeft, timingVirtTop + 28 - sp, valueW, 24, moveFlags);

        // Device group: resize with SWP_FRAMECHANGED so the groupbox border is redrawn correctly
        SetWindowPos(state->hDeviceGroup, nullptr, margin, deviceVirtTop - sp, groupW, deviceH,
            moveFlags | SWP_FRAMECHANGED);
        // Refresh button: top-right inside device group
        if (state->hRefreshButton)
            SetWindowPos(state->hRefreshButton, nullptr, groupW - 80, 20, 64, 26, moveFlags);
        // Move up button
        if (state->hMoveUpButton)
            SetWindowPos(state->hMoveUpButton, nullptr, groupW - 80, 56, 64, 26, moveFlags);
        // Move down button
        if (state->hMoveDownButton)
            SetWindowPos(state->hMoveDownButton, nullptr, groupW - 80, 92, 64, 26, moveFlags);
        // Resize List View
        if (state->hDeviceList)
        {
            int listW = groupW - 100;
            SetWindowPos(state->hDeviceList, nullptr, 16, 20, listW, deviceH - 32, moveFlags);
            ListView_SetColumnWidth(state->hDeviceList, 0, listW - 25);
        }

        // Fixed button bar (not scrolled)
        int right = clientW - margin;
        SetWindowPos(state->hCancelButton, nullptr, right - 80,                         buttonY, 80, buttonH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        SetWindowPos(state->hOkButton,     nullptr, right - 80 * 2 - buttonGap,        buttonY, 80, buttonH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        SetWindowPos(state->hApplyButton,  nullptr, right - 80 * 3 - buttonGap * 2,    buttonY, 80, buttonH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);

        // Full repaint
        InvalidateRect(hWnd, nullptr, TRUE);
        UpdateWindow(hWnd);
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

    LRESULT CALLBACK TokenPromptProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // 简化处理，直接返回FALSE，不创建任何控件
        return FALSE;
    }

    bool PromptTokenDialog(HWND parent, const std::wstring& currentToken, int failedCount, std::wstring& tokenOut)
    {
        // 使用静态变量确保只弹出一次
        static bool hasShownAlert = false;
        
        // 特殊参数用于重置标志
        if (failedCount == -1) {
            hasShownAlert = false;
            tokenOut = currentToken;
            return false;
        }
        
        if (!hasShownAlert) {
            // 使用简单的MessageBox，但在新线程中显示以避免阻塞
            std::thread([parent]() {
                if (!BatteryPlugin::Instance().m_optionsDialogOpening) {
                    MessageBoxW(parent, L"鉴权失败，请在插件设置中填写 Token！", L"设备电量鉴权", MB_OK | MB_ICONWARNING);
                } else {
                    MessageBoxW(parent, L"鉴权失败，请检查 Token 是否正确。", L"设备电量鉴权", MB_OK | MB_ICONWARNING);
                }
            }).detach();
            
            hasShownAlert = true;
        }
        
        tokenOut = currentToken; // 返回原有Token
        return false; // 表示用户没有输入新的Token
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
            state->hRefreshLabel = CreateWindowW(L"STATIC", L"\u6570\u636E\u5237\u65B0\u95F4\u9694\uFF08\u79D2\uFF0C1-3600\uFF09\uFF1A",
                WS_CHILD | WS_VISIBLE, 32, 178, 260, 20, hWnd, nullptr, nullptr, nullptr);

            // Edits
            wchar_t portText[16] = {};
            wchar_t refreshText[16] = {};
            wsprintfW(portText, L"%d", state->currentPort);
            wsprintfW(refreshText, L"%d", state->currentRefreshSec);

            state->hPortEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", portText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 42, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_PORT_EDIT, nullptr, nullptr);
            state->hTokenEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->currentToken.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 86, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_TOKEN_EDIT, nullptr, nullptr);
            state->hRefreshEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", refreshText,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 290, 174, 274, 24,
                hWnd, (HMENU)(INT_PTR)ID_BATTERY_REFRESH_EDIT, nullptr, nullptr);
            SendMessageW(state->hPortEdit, EM_SETLIMITTEXT, 5, 0);
            SendMessageW(state->hTokenEdit, EM_SETLIMITTEXT, 256, 0);
            SendMessageW(state->hRefreshEdit, EM_SETLIMITTEXT, 4, 0);

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
            setFont(state->hRefreshLabel);
            setFont(state->hPortEdit);     setFont(state->hTokenEdit);
            setFont(state->hRefreshEdit);
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
                // 将光标设置到末尾，而不是全选，防止用户打字瞬间被覆盖，同时解决失焦问题
                SendMessageW(state->hPortEdit, EM_SETSEL, -1, -1);
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
                    if (!state->bCheckingLimits && pnmv->iItem >= 0 && pnmv->iItem < (int)state->deviceIds.size())
                    {
                        bool isChecked = ((pnmv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
                        std::wstring deviceId = state->deviceIds[pnmv->iItem];
                        state->plugin->SetDeviceSelection(deviceId, isChecked);
                        state->plugin->SaveConfig();
                        state->plugin->RebuildItems();
                    }

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
            const int notifyCode = HIWORD(wParam);
            
            // Handle edit control change notifications to prevent focus loss
            if (notifyCode == EN_CHANGE)
            {
                switch (cmd)
                {
                case ID_TOKEN_EDIT:
                case ID_PORT_EDIT:
                case ID_BATTERY_REFRESH_EDIT:
                    // Let the default processing handle the text change
                    return 0;
                }
            }
            
            if (cmd == ID_REFRESH_BUTTON && state && state->plugin)
            {
                // 获取当前输入框中的Token和端口值
                wchar_t tokenText[512] = {};
                wchar_t portText[32] = {};
                wchar_t refreshText[32] = {};
                GetWindowTextW(state->hTokenEdit, tokenText, 511);
                GetWindowTextW(state->hPortEdit, portText, 31);
                GetWindowTextW(state->hRefreshEdit, refreshText, 31);

                // 先行验证端口和刷新间隔，避免无效请求
                int port = 0;
                if (!TryParseInteger(portText, 1, 65535, port)) {
                    MessageBoxW(hWnd, L"端口必须是 1 到 65535 之间的整数。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hPortEdit);
                    SendMessageW(state->hPortEdit, EM_SETSEL, 0, -1);
                    return 0;
                }
                
                int refreshSec = 0;
                if (!TryParseInteger(refreshText, 1, 3600, refreshSec)) {
                    MessageBoxW(hWnd, L"刷新间隔必须是 1 到 3600 之间的整数秒。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hRefreshEdit);
                    SendMessageW(state->hRefreshEdit, EM_SETSEL, 0, -1);
                    return 0;
                }

                // 临时更新插件中的配置用于这次请求
                state->plugin->SetApiToken(tokenText);
                state->plugin->SetApiPort(port);
                
                // Force a live API fetch first, then update the checkbox list
                state->plugin->RefreshDevicesNow();
                // 刷新设备列表显示
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
            
            if ((cmd == IDOK || cmd == ID_APPLY_BUTTON) && state && state->hPortEdit && state->hTokenEdit && state->hRefreshEdit)
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
                wchar_t refreshText[32] = {};
                GetWindowTextW(state->hPortEdit, portText, 31);
                GetWindowTextW(state->hTokenEdit, tokenText, 511);
                GetWindowTextW(state->hRefreshEdit, refreshText, 31);

                int parsedPort = 0;
                if (!TryParseInteger(portText, 1, 65535, parsedPort))
                {
                    MessageBoxW(hWnd, L"端口必须是 1 到 65535 之间的整数。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hPortEdit);
                    SendMessageW(state->hPortEdit, EM_SETSEL, 0, -1);
                    return 0;
                }

                int parsedRefreshSec = 0;
                if (!TryParseInteger(refreshText, 1, 3600, parsedRefreshSec))
                {
                    MessageBoxW(hWnd, L"刷新间隔必须是 1 到 3600 之间的整数秒。", L"设备电量", MB_OK | MB_ICONWARNING);
                    SetFocus(state->hRefreshEdit);
                    SendMessageW(state->hRefreshEdit, EM_SETSEL, -1, -1);
                    return 0;
                }

                state->resultPort = parsedPort;
                state->resultToken = tokenText;
                state->resultRefreshSec = parsedRefreshSec;
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

    bool ShowOptionsDialogWindow(HWND parent, int currentPort, const std::wstring& currentToken, int currentRefreshSec, int& resultPort, std::wstring& resultToken, int& resultRefreshSec)
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
        state.currentRefreshSec = currentRefreshSec;
        state.resultRefreshSec = currentRefreshSec;
        state.plugin = &BatteryPlugin::Instance();

        if (ownerWindow) EnableWindow(ownerWindow, FALSE);
        
        {
            std::lock_guard<std::mutex> lock(state.plugin->m_mutex);
            state.plugin->m_optionsDialogOpening = true;
        }

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
            std::lock_guard<std::mutex> lock(state.plugin->m_mutex);
            state.plugin->m_optionsDialogOpening = false;
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

        if (ownerWindow) EnableWindow(ownerWindow, TRUE);
        if (ownerWindow) SetForegroundWindow(ownerWindow);

        {
            std::lock_guard<std::mutex> lock(state.plugin->m_mutex);
            state.plugin->m_optionsDialogOpening = false;
        }

        if (state.accepted)
        {
            resultPort = state.resultPort;
            resultToken = state.resultToken;
            resultRefreshSec = state.resultRefreshSec;
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
    unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastRefreshTick = now;
    }
}

void BatteryPlugin::FetchAndUpdate()
{
    int port = 18080;
    std::wstring token;
    bool disabled = false;
    bool stopApiRequests = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
        token = m_apiToken;
        disabled = m_pluginDisabled;
        stopApiRequests = m_stopApiRequests;
    }
    
    // 如果已经因为鉴权失败而停止API请求，则直接返回
    if (stopApiRequests) {
        return;
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
        bool dialogOpening = false;
        bool alreadyStopped = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            dialogOpening = m_optionsDialogOpening;
            alreadyStopped = m_stopApiRequests;
        }
        
        if (!dialogOpening && !alreadyStopped) {
            // 设置熔断标志点：一旦弹窗，就停止后续自动请求，直到用户手动刷新
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopApiRequests = true;
            }
            
            // 使用独立线程弹出错误框避免阻塞UI和造成主窗口失焦
            std::thread([]() {
                MessageBoxW(nullptr, L"API请求失败，请检查端口设置和EasyBluetooth是否开启。", L"设备电量", MB_OK | MB_ICONWARNING | MB_TOPMOST);
            }).detach();
        }
        
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
            failCount = ++m_authFailCount;
            m_stopApiRequests = true; // 设置停止API请求标志
            
            // 只要鉴权失败，不论Token是否为空，都不再视为首次启动加载
            m_autoSelectFirstDevices = false;
            
            for (auto& item : m_items)
                item->SetOffline();
            if (m_displayItem)
                m_displayItem->SetOffline();
        }

        // 鉴权失败，显示提示框（只弹出一次），但插件继续运行
        bool dialogOpening = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            dialogOpening = m_optionsDialogOpening;
        }

        if (dialogOpening) {
            std::thread([]() {
                MessageBoxW(nullptr, L"鉴权失败：Token 错误或已过期。", L"设备电量", MB_OK | MB_ICONWARNING | MB_TOPMOST);
            }).detach();
        } else {
            PromptTokenDialog(nullptr, token, failCount, token);
        }
        return;
    }

    auto devices = ParseBatteryJson(json);
    
    bool needsRebuild = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_authFailCount = 0;
        m_pluginDisabled = false;
        m_stopApiRequests = false; // 重置停止API请求标志
        m_availableDevices = devices;
        m_refreshDevices = devices; // 同时也同步设置界面的缓存，防止离线设备在任务栏停留
        
        // 成功鉴权，重置提示框标志
        std::wstring dummy;
        PromptTokenDialog(nullptr, L"", -1, dummy);
        
        if (m_autoSelectFirstDevices && !devices.empty()) {
            m_autoSelectFirstDevices = false;
            for (const auto& dev : devices) {
                if (m_selectedDevices.size() >= 4) break;
                // 去重校验
                if (std::find(m_selectedDevices.begin(), m_selectedDevices.end(), dev.id) == m_selectedDevices.end()) {
                    m_selectedDevices.push_back(dev.id);
                }
            }
            needsRebuild = true;
        }
    }
    
    // 无论是否需要重建列表（如自动勾选），每次获取新数据后都重新构建项以更新电量等状态
    RebuildItems();
    
    if (needsRebuild) {
        SaveConfig();
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
        bool dialogOpening = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            dialogOpening = m_optionsDialogOpening;
        }
        if (dialogOpening) return; // 对话框打开期间停止后台自动刷新

        unsigned long long now = GetTickCount64();
        bool needRefresh = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            unsigned long long elapsed = now - m_lastRefreshTick;
            needRefresh = (elapsed >= (unsigned long long)m_refreshIntervalMs);
            if (needRefresh)
            {
                m_lastRefreshTick = now;
            }
        }

        if (needRefresh)
            FetchAndUpdate();
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
    case TMI_VERSION:     return L"1.0.1";
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
    int currentRefreshSec = 2;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        currentPort = m_apiPort;
        currentToken = m_apiToken;
        currentRefreshSec = m_refreshIntervalMs / 1000;
    }

    int newPort = currentPort;
    std::wstring newToken = currentToken;
    int newRefreshSec = currentRefreshSec;
    
    // Track if device selection changed to force restart
    std::vector<std::wstring> oldSelected;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oldSelected = m_selectedDevices;
    }

    bool accepted = ShowOptionsDialogWindow(reinterpret_cast<HWND>(hParent), currentPort, currentToken, currentRefreshSec, newPort, newToken, newRefreshSec);
    if (!accepted)
        return OR_OPTION_UNCHANGED;

    bool changed = (newPort != currentPort) || (newToken != currentToken) || (newRefreshSec != currentRefreshSec);
    if (!changed)
        return OR_OPTION_UNCHANGED;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_apiPort = newPort;
        m_apiToken = newToken;
        m_authFailCount = 0;
        m_pluginDisabled = false;
        m_stopApiRequests = false; // 重置停止API请求标志，允许重新尝试
        m_refreshIntervalMs = newRefreshSec * 1000;
        m_lastRefreshTick = 0;
    }
    
    // Check if the actual selected device set changed
    bool devicesChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        devicesChanged = (oldSelected != m_selectedDevices);
    }
    
    SaveConfig();
    RebuildItems(); // Rebuild items locally so new devices are allocated
    FetchAndUpdate();
    unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastRefreshTick = now;
    }
    
    // 注释掉设备变化重启逻辑，避免不必要的程序重启
    // 用户可以通过手动重启TrafficMonitor来应用设备数量变化
    /*
    if (devicesChanged) {
        RestartTrafficMonitor();
    }
    */
    
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

    int fallbackRefreshSec = 2;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        fallbackRefreshSec = m_refreshIntervalMs / 1000;
    }

    // 优先读取合并后的键值，若不存在则读取旧的电量刷新键值作为回退
    int refreshSec = GetPrivateProfileIntW(L"timing", L"refresh_interval_sec", -1, configPath.c_str());
    if (refreshSec == -1) {
        refreshSec = GetPrivateProfileIntW(L"timing", L"battery_refresh_sec", fallbackRefreshSec, configPath.c_str());
    }
    
    if (refreshSec < 1) refreshSec = 1;
    if (refreshSec > 3600) refreshSec = 3600;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_refreshIntervalMs = refreshSec * 1000;
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
                if (std::find(m_selectedDevices.begin(), m_selectedDevices.end(), deviceId) == m_selectedDevices.end())
                {
                    m_selectedDevices.push_back(deviceId);
                }
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
    int refreshSec = 2;
    std::vector<std::wstring> selectedDevices;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
        token = m_apiToken;
        refreshSec = m_refreshIntervalMs / 1000;
        selectedDevices = m_selectedDevices;
    }
    wchar_t text[16] = {};
    wsprintfW(text, L"%d", port);
    WritePrivateProfileStringW(L"network", L"port", text, configPath.c_str());
    WritePrivateProfileStringW(L"auth", L"token", token.c_str(), configPath.c_str());

    wchar_t refreshText[16] = {};
    wsprintfW(refreshText, L"%d", refreshSec);
    WritePrivateProfileStringW(L"timing", L"refresh_interval_sec", refreshText, configPath.c_str());

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
    
    // We maintain a stable order by iterating the set up to 4 valid items
    std::vector<std::wstring> orderedIds;
    for (const auto& id : m_selectedDevices) {
        if (orderedIds.size() >= 4) break;
        
        bool available = false;
        for (const auto& dev : m_availableDevices) {
            if (dev.id == id) { available = true; break; }
        }
        if (!available) {
            for (const auto& dev : m_refreshDevices) {
                if (dev.id == id) { available = true; break; }
            }
        }
        
        if (available) {
            orderedIds.push_back(id);
        }
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
            bool found = false;
            for (const auto& dev : m_availableDevices) {
                if (dev.id == orderedIds[i]) {
                    m_items[i]->Update(dev);
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (const auto& dev : m_refreshDevices) {
                    if (dev.id == orderedIds[i]) {
                        m_items[i]->Update(dev);
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                m_items[i]->InitWithId(orderedIds[i]); 
            }
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

std::vector<DeviceBattery> BatteryPlugin::GetRefreshDevices() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_refreshDevices;
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
    // 获取当前设置
    int port = 18080;
    std::wstring token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        port = m_apiPort;
        token = m_apiToken;
        // 重置停止标志，允许这次刷新请求
        m_stopApiRequests = false;
    }
    
    // 发起API请求
    std::string json = HttpGet(API_HOST, port, API_PATH, token.empty() ? nullptr : token.c_str(), 3000);
    
    if (json.empty())
    {
        // 使用独立线程弹出错误框避免阻塞UI和造成主窗口失焦
        std::thread([]() {
            MessageBoxW(nullptr, L"API请求失败，请检查端口设置和EasyBluetooth是否开启。", L"设备电量", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }).detach();
        return;
    }
    
    if (IsAuthFailedResponse(json))
    {
        // 鉴权失败，提示错误并停止后续自动请求
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopApiRequests = true;
        m_authFailCount++;
        m_autoSelectFirstDevices = false; // 手动刷新失败也取消自动勾选，后续由用户手动选择
        
        // 使用独立线程弹出错误框避免阻塞UI和造成主窗口失焦
        std::thread([]() {
            MessageBoxW(nullptr, L"鉴权失败Token错误", L"设备电量", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }).detach();
        return;
    }
    
    // 请求成功，解析设备数据
    auto devices = ParseBatteryJson(json);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_authFailCount = 0;
        m_pluginDisabled = false;
        m_stopApiRequests = false; // 确保后续自动请求正常
        m_availableDevices = devices;      // 用于插件主界面自动请求
        m_refreshDevices = devices;       // 用于刷新按钮显示（独立存储）
        
        // 手动刷新时保持用户之前的选择，不做自动勾选
        m_autoSelectFirstDevices = false; // 取消首次启动标志

        
        // 重置提示框标志
        std::wstring dummy;
        PromptTokenDialog(nullptr, L"", -1, dummy);
    }
}

void BatteryPlugin::SetApiToken(const std::wstring& token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_apiToken = token;
}

void BatteryPlugin::SetApiPort(int port)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_apiPort = port;
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
