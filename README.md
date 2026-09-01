# ESP32 Wi-Fi UART Bridge

ESP32-S3 双向 UART—TCP 桥接 Demo。UART RX 数据经 Wi-Fi TCP 上传，TCP 下行数据经 UART TX 输出。

完整的设计、配置、构建方式和开发记录见 [`doc/README.md`](doc/README.md)。

## 依赖

本项目不复制组件源码，构建时通过 `EXTRA_COMPONENT_DIRS` 引用以下两个组件的唯一实现：

- `WIFI_MODE_DSS/.component/wifi_module`
- `UART_T_T/.component/uart_pt`

请将 `WIFI_UART_BRIDGE`、`WIFI_MODE_DSS` 和 `UART_T_T` 放在同一父目录下。单独克隆本仓库时，还需准备另外两个组件项目，或按实际目录调整 `src/WIFI_UART_BRIDGE/CMakeLists.txt` 中的组件路径。

## 配置与构建

仓库不保存现场 Wi-Fi 凭据，`sdkconfig` 仅保留在本机且被 Git 忽略。首次使用时执行：

```powershell
.\scripts\configure.ps1
.\scripts\build.ps1
```

默认构建产物位于 `src/WIFI_UART_BRIDGE/build/`。

