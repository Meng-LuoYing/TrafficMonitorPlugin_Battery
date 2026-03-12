#pragma once

// 定义 WIN32_LEAN_AND_MEAN 以减少 Windows 头文件包含的大小
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Windows 核心头文件
#include <windows.h>
#include <commctrl.h>    // 通用控件
#include <winhttp.h>     // HTTP 功能

// 标准库头文件
#include <string>
#include <vector>
#include <mutex>        // 线程同步
