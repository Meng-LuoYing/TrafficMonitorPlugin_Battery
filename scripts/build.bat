@echo off
setlocal

echo ==========================================
echo TrafficMonitor Battery Plugin Builder
echo ==========================================

REM Set project root relative to this script
set "PROJECT_ROOT=%~dp0.."
pushd "%PROJECT_ROOT%"

REM Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo CMake not found. Falling back to direct g++ compilation...
    goto :DIRECT_BUILD
)

echo.
echo Cleaning previous build...
if exist build rmdir /s /q build
mkdir build
cd build

echo.
echo Configuring with CMake...
cmake -G "MinGW Makefiles" ..
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo CMake configuration failed. Trying direct g++ compilation...
    cd ..
    goto :DIRECT_BUILD
)

echo.
echo Building...
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo CMake build failed!
    cd ..
    goto :DIRECT_BUILD
)

echo.
echo Build successful (CMake)!
REM Copy to bin folder if needed
if not exist ..\bin mkdir ..\bin
copy /Y *.dll ..\bin\
echo Plugin location: %PROJECT_ROOT%\bin\BatteryPlugin.dll
echo.
echo IMPORTANT: Rename libBatteryPlugin.dll to BatteryPlugin.dll if needed.
pause
popd
exit /b 0

:DIRECT_BUILD
echo.
echo Attempting direct compilation with g++...
if not exist bin mkdir bin
g++ -shared -o bin/BatteryPlugin.dll src/BatteryPlugin.cpp src/dllmain.cpp -Iinclude -lwinhttp -static-libgcc -static-libstdc++ -Wl,--add-stdcall-alias
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Direct build failed! Please check if g++ is installed and in PATH.
    pause
    popd
    exit /b 1
)

echo.
echo Build successful (g++)!
echo Plugin created at: %PROJECT_ROOT%\bin\BatteryPlugin.dll
echo.
pause
popd
exit /b 0
