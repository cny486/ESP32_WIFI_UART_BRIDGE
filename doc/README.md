# WIFI_UART_BRIDGE 总说明

## 项目功能

本项目是同时引用 `wifi_module` 与 `uart_pt` 两个独立 ESP-IDF component 的集成 Demo。UART RX 数据进入 Wi-Fi TCP 上传队列；Wi-Fi TCP 下行协议数据通过 UART TX 输出，形成双向 UART—TCP 桥接。

## 当前状态

- 2026-09-01 01:03 +0800：创建 ESP32-S3 集成项目，同时通过 `EXTRA_COMPONENT_DIRS` 引用 `WIFI_MODE_DSS/.component/wifi_module` 与 `UART_T_T/.component/uart_pt`。完成双向 UART—TCP 桥接、Kconfig 参数和配置/构建/清理脚本。首次配置因全新工程无法发现 Ninja 而中止，随后显式配置 Ninja、ESP32-S3 工具链和 bootloader 子工程；`scripts/build.ps1` 已在 ESP-IDF v6.1 下全量构建通过，分别生成并链接 `libwifi_module.a`、`libuart_pt.a`。固件 `WIFI_UART_BRIDGE.bin` 为 `0xc7550` 字节，默认 app 分区剩余 22%；硬件测试待执行。
- 2026-09-01 01:16 +0800：写入指定 Wi-Fi 与 `192.168.137.201:54321` TCP 配置；透传改用 UART0 原生引脚 GPIO44 RXD0、GPIO43 TXD0，分别连接对端 `UART0_MISO`、`UART0_MOSI`。为避免 UART0 日志污染透传，将主控制台切换到 USB Serial/JTAG。UART 上行通过 `wifi_upload_uart_endpoint()` 适配并调用 Wi-Fi TCP 上传队列。已在 ESP-IDF v6.1 下完成全量构建，`wifi_module`、`uart_pt` 和适配入口均编译链接通过；固件为 `0xc65b0` 字节，默认 app 分区剩余 23%，硬件联调待执行。
- 2026-09-01 01:37 +0800：在 UART 上行适配入口增加 RX 数据日志。每个新接收消息通过 USB Serial/JTAG 输出消息 ID、长度、可读 ASCII 和完整十六进制内容；不可打印字符在 ASCII 视图中显示为 `.`，上行重试不重复打印同一消息。已通过 ESP-IDF v6.1 全量构建，固件为 `0xc6930` 字节，默认 app 分区剩余 22%；硬件日志验证待执行。
- 2026-09-01 01:46 +0800：为排查 UART0 无接收问题，只将透传控制器切换为 UART1，GPIO 保持 GPIO43/TX、GPIO44/RX 不变，通过 ESP32-S3 GPIO Matrix 路由 UART1 信号，因此无需改变现有接线；USB Serial/JTAG 日志设置保持不变。ESP-IDF v6.1 全量构建通过，固件为 `0xc6930` 字节，默认 app 分区剩余 22%；硬件验证待执行。
- 2026-09-01 01:50 +0800：按现场调试要求交换 UART1 引脚方向，改为 GPIO44/TX、GPIO43/RX；控制器、波特率、USB Serial/JTAG 日志和透传逻辑均保持不变。对端发送线应接 GPIO43，对端接收线应接 GPIO44。ESP-IDF v6.1 全量构建通过，固件为 `0xc6930` 字节，默认 app 分区剩余 22%；硬件验证待执行。
- 2026-09-01 02:00 +0800：根据 MAX3490 实际逻辑信号恢复 UART1 为 GPIO43/TX、GPIO44/RX，使 GPIO43 连接 MAX3490 DI、GPIO44 连接 MAX3490 RO；其余网络、UART参数和日志逻辑保持不变。已与启动配置日志一起完成构建验证。
- 2026-09-01 02:04 +0800：增加启动配置列表日志，通过 USB Serial/JTAG 打印 Wi-Fi SSID、TCP/UDP参数、重试参数、UART控制器、引脚、波特率、缓冲块和超时配置。Wi-Fi密码只显示是否已配置，不输出明文。ESP-IDF v6.1 全量构建通过，最终有效配置确认为 UART1、GPIO43/TX、GPIO44/RX；固件为 `0xc6c60` 字节，默认 app 分区剩余22%，硬件验证待执行。
- 2026-09-01 02:28 +0800：现场测得 MAX3490 RO 已有波形，并确认 RO 实际连接 GPIO43。将 UART1 RX 改为 GPIO43，同时将 TX 改为连接 DI 的 GPIO44，最终路由为 GPIO44/TX、GPIO43/RX；启动配置列表同步显示新值。ESP-IDF v6.1 全量构建通过，固件为 `0xc6c60` 字节，默认app分区剩余22%；硬件接收验证待执行。
- 2026-09-01 18:07 +0800：为 GitHub 初次发布补充仓库根说明、忽略规则和可重复执行的 `scripts/publish-github.ps1`。本机 `sdkconfig` 及构建产物不进入版本库，`sdkconfig.defaults` 已移除现场 SSID、密码和 TCP 地址；本机有效配置保持不变。桥接源码和组件引用架构未改动。

## 设计

### 功能：同时引用两个独立组件

- **目标**：不复制 Wi-Fi 和 UART 组件源码，通过 CMake 外部组件路径直接复用仓库中的唯一实现，并验证两个组件能在同一固件中配置、编译和链接。
- **依赖设计**：项目顶层 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 同时指向两个 `.component` 子目录；应用 `main` 通过 `PRIV_REQUIRES wifi_module uart_pt` 声明直接依赖。
- **目录约束**：集成项目只保存桥接业务和自身配置，不修改、镜像或混放组件源码。

### 功能：UART—TCP 双向桥接

- **UART 到 TCP**：`uart_pt` 通过 `uart_pt_bind_endpoint(wifi_upload_uart_endpoint, NULL)` 注册 Wi-Fi 上传适配端点；端点将每个 RX 消息块交给 `wifi_module_add_to_tcp_queue()`。成功入队后释放 UART 消息块，TCP 队列忙或网络模块不可用时返回 BUSY/OFFLINE，由 UART 组件按配置重试。
- **TCP 到 UART**：`wifi_module` 的协议回调将 ACK、AT、TIME、CFG、UUID、OTA 和 RAW 数据通过 `uart_pt_send()` 写入 UART。Wi-Fi 组件内部消费的 `HRT` 心跳只回复 `ALE`，不会进入 UART。
- **启动顺序**：先初始化并启动 UART，确保 TCP 下行到达时 UART 已可用；再初始化 Wi-Fi 并自动连接服务器。Wi-Fi 初始化失败时停止 UART 任务。
- **配置方式**：SSID、密码、TCP/UDP 参数以及 UART 控制器、引脚、波特率、块数和重试次数全部由项目 Kconfig 管理，不写死在桥接源码中。
- **UART1 与 MAX3490**：透传使用 UART1，通过 ESP32-S3 GPIO Matrix 将 GPIO44 配置为 TX并连接 MAX3490 DI，将 GPIO43 配置为 RX并连接 MAX3490 RO。主日志控制台继续使用 USB Serial/JTAG，不占用透传 UART。
- **RX 数据观察**：UART 上行端点在首次处理消息时以 INFO 级别输出消息 ID、长度、ASCII 和十六进制数据。日志仅走 USB Serial/JTAG；同一消息因上传忙而重试时不重复输出。
- **启动配置观察**：`app_main()` 初始化组件前调用 `log_bridge_configuration()`，通过 USB Serial/JTAG 输出当前有效网络和UART配置；密码只输出 `<configured>` 或 `<empty>` 状态。
- **边界**：本项目验证组件集成和双向数据路径；不改变两个组件内部的单实例、队列、重连或协议分流行为。

## 架构

- `src/WIFI_UART_BRIDGE/`：独立 ESP-IDF 工程。
- `src/WIFI_UART_BRIDGE/main/main.c`：两个组件的适配与启动入口。
- `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild`：网络和 UART 参数。
- `src/WIFI_UART_BRIDGE/build/`：ESP-IDF 默认构建产物目录。
- `scripts/build.ps1`：配置并构建项目，检查每一步退出码。
- `scripts/configure.ps1`：打开项目的 `menuconfig`，配置网络和 UART 参数。
- `scripts/clean.ps1`：清理 ESP-IDF 默认构建目录。
- `scripts/publish-github.ps1`：检查凭据、提交并推送项目到约定的 GitHub 远端，不执行强制推送。
- `doc/`：总说明和单次开发记录。

## 具体实现

`log_bridge_configuration()` 在启动组件前打印当前有效配置。`wifi_upload_uart_endpoint()` 是 UART 上行端点，先调用 `log_uart_rx_message()` 输出首次接收的数据，再把组件提供的消息块复制进入 TCP 队列。`bridge_wifi_to_uart()` 是 Wi-Fi 协议回调，把下行数据同步写入 UART。`bridge_network_event()` 通过 USB Serial/JTAG 控制台输出 Wi-Fi、IP、TCP 和 UDP 状态快照，便于联调。

## 配置方式

在模块根目录执行以下脚本，打开 `menuconfig` 的 `Wi-Fi UART bridge configuration`，设置实际 Wi-Fi、TCP 服务器和 UART 参数：

```powershell
.\scripts\configure.ps1
```

当前项目已经保存指定的 Wi-Fi、TCP 和 UART1 参数；如现场参数变化，可通过该脚本重新配置。

现场参数保存在本机 `sdkconfig` 中，该文件已被 Git 忽略；仓库的 `sdkconfig.defaults` 只提供无凭据模板。新克隆的工作区必须先配置自己的 Wi-Fi 和服务器参数。

## 构建方式

在模块根目录执行：

```powershell
.\scripts\build.ps1
```

构建脚本显式配置本机 Ninja、ESP32-S3 工具链和 bootloader 子工程，关闭 ccache、限制并发并检查退出码。构建产物保存在 `src/WIFI_UART_BRIDGE/build/`。本项目未提供自动烧录操作；硬件烧录和连线测试由用户确认设备与串口后执行。
