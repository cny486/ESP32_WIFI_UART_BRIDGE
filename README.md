# ESP32 Wi-Fi UART Bridge

ESP32-S3 双向 UART—TCP 桥接 Demo。UART RX 数据经 Wi-Fi TCP 上传，TCP 下行数据经 UART TX 输出。

完整的设计、配置、构建方式和开发记录见 [`doc/README.md`](doc/README.md)。

## 自包含组件

项目已在 ESP-IDF 工程的标准 `components/` 目录中包含完整组件源码：

- `src/WIFI_UART_BRIDGE/components/wifi_module`
- `src/WIFI_UART_BRIDGE/components/uart_pt`

ESP-IDF 会自动发现这两个组件，因此只需克隆本仓库，无需准备其它同级项目或调整 `EXTRA_COMPONENT_DIRS`。

## 配置与构建

宿主机需要自行安装 ESP-IDF v6.1。首次使用项目脚本前，保存本机 ESP-IDF profile 路径：

```powershell
.\scripts\set-esp-idf-paths.ps1 `
  -EspIdfProfile "C:\你的安装目录\ESP-IDF-PowerShell-profile.ps1"
```

安装程序没有把 Ninja、工具链、CMake 或 Python 加入 profile 时，可同时传入 `-NinjaPath`、`-ToolchainBin`、`-CMakePath` 和 `-PythonPath`。配置保存在被 Git 忽略的 `scripts/esp-idf.paths.local.ps1`；字段说明也可参考 `scripts/esp-idf.paths.example.ps1`。如果当前终端已经激活 ESP-IDF，可以不创建本地路径文件。

仓库不保存现场 Wi-Fi 凭据，`sdkconfig` 仅保留在本机且被 Git 忽略。路径配置完成后执行：

```powershell
.\scripts\configure.ps1
.\scripts\build.ps1
```

也可以在已激活 ESP-IDF v6.1 环境的终端中使用标准命令：

```powershell
cd .\src\WIFI_UART_BRIDGE
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

在 `menuconfig` 的 `Wi-Fi UART bridge configuration` 中设置自己的 Wi-Fi、TCP 服务器和 UART 参数。项目目标为 ESP32-S3，当前使用 ESP-IDF v6.1 验证通过。

默认构建产物位于 `src/WIFI_UART_BRIDGE/build/`。
