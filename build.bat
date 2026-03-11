@echo off
setlocal

:: 创建 build 目录
if not exist build (
    mkdir build
)

:: 进入 build 目录
cd build

:: 运行 CMake 生成构建文件
cmake ..

:: 检查 cmake 是否成功
if %errorlevel% neq 0 (
    echo CMake generation failed!
    exit /b %errorlevel%
)

:: 构建项目 (Release 配置)
cmake --build . --config Release

:: 检查构建是否成功
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)

echo Build successful!
endlocal
