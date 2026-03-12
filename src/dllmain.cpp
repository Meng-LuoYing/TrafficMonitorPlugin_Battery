#include "pch.h"

// DLL 入口点函数
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:  // 进程附加
    case DLL_THREAD_ATTACH:   // 线程附加
    case DLL_THREAD_DETACH:   // 线程分离
    case DLL_PROCESS_DETACH:  // 进程分离
        break;
    }
    return TRUE;
}
