# TrafficMonitor Battery Plugin

这是一个为 TrafficMonitor 开发的电量显示插件。它从 `http://127.0.0.1:18080/api/v1/status` 获取设备电量信息并显示。

## 目录结构

- `PluginInterface.h`: 插件接口定义
- `BatteryPlugin.h`: 插件类定义
- `BatteryPlugin.cpp`: 插件实现
- `dllmain.cpp`: DLL 入口点
- `build.bat`: **一键构建脚本**
- `CMakeLists.txt`: CMake 构建配置文件

## 如何构建

### 方法一：使用一键构建脚本 (推荐)

双击运行目录下的 `build.bat` 文件。
脚本会自动检测你的环境（优先使用 CMake，如果失败则尝试直接使用 g++ 编译）。

构建成功后，会在当前目录下生成 `TrafficMonitorBatteryPlugin.dll` 文件。

### 方法二：手动编译 (MinGW/g++)

在终端中运行以下命令：

```bash
g++ -shared -o TrafficMonitorBatteryPlugin.dll BatteryPlugin.cpp dllmain.cpp -lwinhttp -static-libgcc -static-libstdc++ -Wl,--add-stdcall-alias
```

### 方法三：使用 Visual Studio

1. 创建一个新的 **动态链接库 (DLL)** C++ 项目。
2. 将所有源文件 (`.h`, `.cpp`) 添加到项目中。
3. 链接库设置：添加 `winhttp.lib`。
4. 生成解决方案 (Release x64/x86)。

## 安装插件

1. 将生成的 `TrafficMonitorBatteryPlugin.dll` 复制到 TrafficMonitor 安装目录下的 `plugins` 文件夹中。
   - 如果 `plugins` 文件夹不存在，请手动创建。
2. 重启 TrafficMonitor。
3. 在 TrafficMonitor 主窗口右键菜单中，选择 **更多功能** -> **插件管理**，确认插件已加载。
4. 在任务栏窗口右键菜单中，选择 **显示设置**，勾选 **设备电量**。

## 常见问题

- **插件未显示**: 确保编译的架构 (x64/x86) 与 TrafficMonitor 主程序一致。如果不确定，可以尝试编译另一种架构。
- **乱码**: 插件内部已处理 UTF-8 到 WideChar 的转换，确保 API 返回的是标准 JSON 格式。
