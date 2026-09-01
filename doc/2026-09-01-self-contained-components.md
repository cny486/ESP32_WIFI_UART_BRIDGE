# 内置组件与独立构建

## 本次目标

把 `wifi_module` 和 `uart_pt` 的完整组件源码复制到本项目 ESP-IDF 工程的标准 `components/` 目录，移除对同级 `WIFI_MODE_DSS`、`UART_T_T` 项目的构建依赖，使其他人只克隆本仓库即可配置并构建。

## 涉及文件

| 文件或目录 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/components/wifi_module/` | 纳入组件 CMake、公共头文件、实现源码和 README。 |
| `src/WIFI_UART_BRIDGE/components/uart_pt/` | 纳入组件 CMake、公共头文件、实现源码和 README。 |
| `src/WIFI_UART_BRIDGE/CMakeLists.txt` | 删除指向仓库外组件目录的 `EXTRA_COMPONENT_DIRS`。 |
| `README.md` | 改为说明仓库已自包含两个组件，可直接克隆。 |
| `doc/README.md` | 更新项目功能、设计、架构和当前状态。 |

## 功能与架构影响

桥接业务 API、启动顺序、UART 引脚和网络数据路径均未改变。组件发现方式从“引用同级项目中的 `.component`”改为“由 ESP-IDF 自动扫描本工程 `components/`”。因此 GitHub 仓库不再依赖原开发工作区的目录结构。

仓库内的组件成为本项目当前可构建版本；原组件项目后续更新不会自动同步。每次同步组件时都应重新运行构建脚本，并在开发记录中说明组件版本变化。

## 使用方式

克隆后在仓库根目录执行：

```powershell
.\scripts\configure.ps1
.\scripts\build.ps1
```

必须先设置自己的 Wi-Fi SSID、密码和 TCP 服务器。现场配置保存在被 Git 忽略的 `sdkconfig`，不会上传到远端。

## 构建验证

使用 `scripts/build.ps1` 在 ESP-IDF v6.1、ESP32-S3 目标上进行全量配置和构建。构建日志需要确认组件来源是本项目 `components/`，并生成、链接 `libwifi_module.a` 与 `libuart_pt.a`。硬件联网、TCP 通信和 UART 收发仍需烧录后验证。

实际验证已完成：CMake 的组件路径明确指向本仓库中的 `src/WIFI_UART_BRIDGE/components/uart_pt` 与 `components/wifi_module`；两份组件源码分别编译生成并链接 `libuart_pt.a` 和 `libwifi_module.a`。最终生成 `WIFI_UART_BRIDGE.bin`，大小为 `0xc6c60` 字节，默认 `0x100000` app 分区剩余 `0x393a0` 字节（22%）。本次只完成编译验证，尚未进行硬件烧录与端到端通信验证。
