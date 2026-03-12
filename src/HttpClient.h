#pragma once
#include <string>

// 单次同步 HTTP GET 请求，返回响应体字符串
// 失败时返回空字符串
std::string HttpGet(const wchar_t* host, int port, const wchar_t* path, const wchar_t* token = nullptr, int timeout_ms = 3000);
