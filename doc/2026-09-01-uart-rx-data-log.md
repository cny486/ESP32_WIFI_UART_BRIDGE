# UART 接收数据日志

## 本次目标

在不占用 UART0 TXD0、且不改变 UART—TCP 透传数据的前提下，通过 USB Serial/JTAG 日志观察 UART0 收到的原始数据。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/main/main.c` | 新增 `log_uart_rx_message()`，并在 Wi-Fi 上传适配入口打印首次接收的 UART 消息。 |
| `doc/README.md` | 更新当前状态、日志设计和具体实现。 |

## 实现说明

每个新 UART 消息在进入 TCP 上传队列前输出两种视图：

- INFO 摘要：消息 ID、数据长度和 ASCII 内容；`0x20` 至 `0x7e` 之外的字节显示为 `.`。
- 十六进制视图：使用 ESP-IDF 日志接口输出全部消息字节，便于检查二进制协议和不可打印字符。

日志通过 USB Serial/JTAG 主控制台输出，不会写入 GPIO43/TXD0。只有 `retry_count == 0` 的首次上行调用打印数据，因此 TCP 队列繁忙导致的重试不会重复打印同一消息。

## 架构影响

改动只位于集成项目的 UART→Wi-Fi 适配层，不修改 `uart_pt` 或 `wifi_module` 公共组件，因此其它引用这两个组件的项目不受影响。透传数据本身不被修改。

## 构建与验证

已使用 `scripts/build.ps1` 完成 ESP-IDF v6.1、ESP32-S3 全量构建：`main.c`、`libwifi_module.a` 和 `libuart_pt.a` 均编译链接通过；生成的 `WIFI_UART_BRIDGE.bin` 为 `0xc6930` 字节（813360 字节），默认 app 分区剩余 22%。

发送 ASCII 数据 `Hello` 时，USB Serial/JTAG 上的主要日志格式如下（时间戳和消息 ID以运行时输出为准）：

```text
I (...) wifi_uart_bridge: UART RX: id=1 length=5 ASCII="Hello"
I (...) wifi_uart_bridge: 0x...   48 65 6c 6c 6f   |Hello|
```

实际 UART0 数据内容和 USB Serial/JTAG 输出仍需烧录后进行硬件验证。
