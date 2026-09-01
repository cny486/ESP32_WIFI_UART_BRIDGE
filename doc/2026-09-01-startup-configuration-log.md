# 启动配置列表日志

## 本次目标

在固件启动时通过 USB Serial/JTAG 打印当前有效配置，方便烧录后直接核对网络、UART和重试参数。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/main/main.c` | 新增 `log_bridge_configuration()` 并在 `app_main()` 初始化组件前调用。 |
| `doc/README.md` | 更新当前状态、设计和具体实现。 |

## 输出内容

- Wi-Fi SSID以及密码是否已配置。
- TCP服务器地址和端口、UDP监听端口。
- Wi-Fi/TCP重试次数和TCP重试间隔。
- UART控制器、TX/RX GPIO、波特率和8-N-1格式。
- UART固定块数量、上行重试次数和TX锁超时。
- 日志控制台类型。

密码不打印明文，只显示 `<configured>` 或 `<empty>`，避免调试日志泄露凭据。

## 架构影响

配置打印发生在组件初始化前，不修改配置值、组件状态或UART—TCP数据路径。

## 构建与验证

已使用 `scripts/build.ps1` 完成ESP-IDF v6.1、ESP32-S3全量构建；生成的 `WIFI_UART_BRIDGE.bin` 为 `0xc6c60` 字节（814176字节），默认app分区剩余22%。配置列表函数和两个外部组件均编译链接通过，实际启动日志需烧录后通过USB Serial/JTAG验证。
