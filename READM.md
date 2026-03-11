# TrafficMonitor Battery Plugin

这是一个适配 TrafficMonitor 插件接口 API v7 的设备电量插件。  
插件会定时请求本地接口 `http://127.0.0.1:18080/api/v1/status`，解析设备列表并在 TrafficMonitor 中显示每个设备的电量状态。

## 当前项目结构

- `include/PluginInterface.h`：TrafficMonitor 插件接口定义（API v7）
- `src/BatteryPlugin.cpp`：插件主逻辑，拉取数据并分发到各个显示项
- `src/BatteryItem.cpp`：单个设备电量项的显示与文本格式化
- `src/HttpClient.cpp`：基于 WinHTTP 的本地 HTTP 请求
- `src/JsonParser.cpp`：JSON 解析与 UTF-8 转宽字符处理
- `src/dllmain.cpp`：DLL 入口
- `CMakeLists.txt`：CMake 构建配置
- `build.bat`：一键构建脚本（Release）

## 接口数据要求

插件默认读取以下地址：

- `Host`: `127.0.0.1`
- `Port`: `18080`（可在插件选项中自定义）
- `Path`: `/api/v1/status`

JSON 中会从 `devices` 数组提取设备信息，主要字段包括：

- `id`
- `renamedName`（优先）或 `name`
- `battery`
- `isCharging`
- `status`（`online` 视为在线）
- `isBatteryUnsupported`（为 `true` 的设备会被过滤）

## 构建方式

### 方法一：一键脚本（推荐）

在项目根目录执行：

```bat
build.bat
```

脚本会执行：

```bat
cmake ..
cmake --build . --config Release
```

默认输出位于 `build/Release/BatteryPlugin.dll`（多配置生成器）或 `build/BatteryPlugin.dll`（单配置生成器）。

### 方法二：手动 CMake

```bat
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## 安装与启用

1. 将 `BatteryPlugin.dll` 复制到 TrafficMonitor 安装目录下的 `plugins` 文件夹。
2. 重启 TrafficMonitor。
3. 在主窗口右键菜单中进入 **更多功能 -> 插件管理**，确认插件已加载。
4. 在任务栏窗口右键菜单中进入 **显示设置**，勾选对应的电量显示项。

## 插件选项（端口设置）

1. 在 TrafficMonitor 中打开 **选项 -> 插件选项...**。
2. 选择 **BLE Battery**，输入本地 API 端口（`1-65535`）。
3. 点击确定后立即生效，并会自动保存到插件配置文件。

配置文件位置为 TrafficMonitor 的插件配置目录下：

- `BatteryPlugin.ini`
- 节点：`[network]`
- 字段：`port`

## 常见问题

- **插件未显示**：确认 DLL 架构（x64/x86）与 TrafficMonitor 主程序一致。
- **一直离线或无数据**：确认插件选项中的端口与本地服务一致，并检查接口可访问且返回含 `devices` 的 JSON。
- **中文显示异常**：确认接口返回为 UTF-8 编码 JSON。
