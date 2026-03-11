#include "pch.h"
#include "HttpClient.h"
#include <winhttp.h>

std::string HttpGet(const wchar_t* host, int port, const wchar_t* path, int timeout_ms)
{
    std::string result;

    HINTERNET hSession = WinHttpOpen(
        L"BatteryPlugin/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession)
        return result;

    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return result;
    }

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

    BOOL bSent = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (bSent)
    {
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

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}
