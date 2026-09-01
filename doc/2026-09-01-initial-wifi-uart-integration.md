# Wi-Fi 与 UART 双组件集成

## 本次目标

创建新的 `WIFI_UART_BRIDGE` ESP32-S3 项目，同时引用仓库中已经独立封装的 `wifi_module` 和 `uart_pt`，以双向 UART—TCP 桥接验证两个 component 可以在同一项目中协同工作。

## 涉及文件

| 文件 | 内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/CMakeLists.txt` | 固定 ESP32-S3 目标并加载两个外部 component。 |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 保存默认目标配置。 |
| `src/WIFI_UART_BRIDGE/main/CMakeLists.txt` | 声明 `wifi_module`、`uart_pt` 和日志依赖。 |
| `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild` | 定义网络服务器和 UART 可配置参数。 |
| `src/WIFI_UART_BRIDGE/main/main.c` | 实现 UART→TCP、TCP→UART 适配回调和启动流程。 |
| `scripts/build.ps1` | 新增可重复构建脚本，显式配置 Ninja、ESP32-S3 工具链和 bootloader 子工程，并检查每一步退出码。 |
| `scripts/configure.ps1` | 新增网络与 UART 参数的 `menuconfig` 脚本入口。 |
| `scripts/clean.ps1` | 新增构建目录清理脚本。 |
| `doc/README.md` | 新增项目总设计、架构、使用和状态说明。 |

## 功能与架构影响

这是独立的新集成项目，不修改两个被引用组件的内部行为。项目通过相对路径使用仓库内唯一组件源码；以后组件更新后，重新构建本项目即可验证组合兼容性。

桥接的数据方向为：

```text
UART RX -> uart_pt 上行端点 -> wifi_module TCP 队列 -> TCP 服务器
TCP 下行 -> wifi_module 协议回调 -> uart_pt_send -> UART TX
```

## 使用说明

烧录前在项目的 `menuconfig` 中替换默认 Wi-Fi 凭据、TCP 主机地址，并确认 UART 控制器、GPIO 和波特率与硬件一致。构建使用模块根目录下的 `scripts/build.ps1`。

## 构建与验证

计划使用 `scripts/build.ps1` 进行 ESP-IDF v6.1、ESP32-S3 全量构建，验证两个外部组件的发现、公共头文件、依赖传递和最终链接。硬件联网及 UART 双向收发不在无设备构建验证范围内。

首次执行时，全新构建目录无法从当前 ESP-IDF PowerShell 环境发现 Ninja，配置在编译前中止。构建脚本随后补充了本机 Ninja、ESP32-S3 编译器和 bootloader ExternalProject 的显式路径，第二次执行完成全量验证：

- ESP-IDF 同时识别 `wifi_module` 和 `uart_pt`，路径分别指向两个已有项目的 `.component` 目录；
- `main` 的直接私有依赖同时包含 `wifi_module`、`uart_pt` 和 `log`；
- `wifi_module.c` 与 `uart_pt.c` 分别生成并链接 `libwifi_module.a`、`libuart_pt.a`；
- 桥接 `main.c` 编译、应用链接和镜像尺寸检查全部通过；
- 生成 `src/WIFI_UART_BRIDGE/build/WIFI_UART_BRIDGE.bin`，大小为 `0xc7550`（816464）字节；默认 `0x100000` app 分区剩余 `0x38ab0` 字节（22%）。

构建日志中的 ESP-IDF 仓库 ownership、框架内部 private include 和 Kconfig 默认值警告来自本机框架环境，不影响本次结果。未烧录设备，因此实际 Wi-Fi/TCP 连接、UART 电气连接及双向数据正确性仍待硬件验证。默认分区余量为 22%，后续增加较大功能时需要继续关注固件尺寸或调整分区表。
