cmake_minimum_required(VERSION 3.16)
project(BatteryPlugin LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件列表
set(SOURCES
    src/dllmain.cpp
    src/HttpClient.cpp
    src/JsonParser.cpp
    src/BatteryItem.cpp
    src/BatteryPlugin.cpp
)

# 生成 DLL
add_library(BatteryPlugin SHARED ${SOURCES})

# 包含目录
target_include_directories(BatteryPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接 WinHTTP 和 ComCtl32 (用于 SetWindowSubclass)
target_link_libraries(BatteryPlugin PRIVATE winhttp comctl32)

# Unicode 支持
target_compile_definitions(BatteryPlugin PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WINDOWS
    _USRDLL
)

# MSVC 特定设置
if(MSVC)
    # 使用多线程运行时（Release 用 /MT，Debug 用 /MTd）
    set_property(TARGET BatteryPlugin PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # /utf-8：让 MSVC 以 UTF-8 读写源文件（修复中文注释在 GBK 代码页下乱码）
    # /wd4819：消除"文件包含不能在当前代码页中表示的字符"警告
    # /wd4100：消除未引用形参警告
    target_compile_options(BatteryPlugin PRIVATE /W3 /utf-8 /wd4819 /wd4100)

    # 输出目录
    set_target_properties(BatteryPlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
    )
endif()
