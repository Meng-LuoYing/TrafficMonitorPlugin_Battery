#include "pch.h"
#include "HttpClient.h"
#include <winhttp.h>

// HTTP GET 请求实现
std::string HttpGet(const wchar_t* host, int port, const wchar_t* path, const wchar_t* token, int timeout_ms)
{
    std::string result;

    // 创建 WinHTTP 会话
    HINTERNET hSession = WinHttpOpen(
        L"BatteryPlugin/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession)
        return result;

    // 连接到服务器
    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return result;
    }

    // 创建请求句柄
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0); // 0 = HTTP（非 HTTPS）
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // 设置超时
    WinHttpSetTimeouts(hRequest, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    // 构建请求头
    std::wstring headers;
    if (token != nullptr && token[0] != L'\0')
    {
        headers = L"X-Api-Token: ";
        headers += token;
        headers += L"\r\n";
    }

    // 发送请求
    BOOL bSent = WinHttpSendRequest(
        hRequest,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : static_cast<DWORD>(-1L),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (bSent)
    {
        // 接收响应
        if (WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD dwSize = 0;
            do
            {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                    break;
                if (dwSize == 0)
                    break;

                std::string buf(dwSize, '\0');
                DWORD dwDownloaded = 0;
                if (WinHttpReadData(hRequest, &buf[0], dwSize, &dwDownloaded))
                    result.append(buf, 0, dwDownloaded);
                else
                    break;
            } while (dwSize > 0);
        }
    }

    // 清理资源
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}
