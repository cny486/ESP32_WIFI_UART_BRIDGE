# 恢复 UART1 与 MAX3490 信号方向

## 本次目标

按 MAX3490 的数字接口方向，将 UART1 恢复为 GPIO43/TX、GPIO44/RX，使ESP32发送信号进入 DI、接收信号来自RO。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 恢复默认 TX=GPIO43、RX=GPIO44。 |
| `src/WIFI_UART_BRIDGE/sdkconfig` | 同步当前构建使用的有效配置。 |
| `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild` | 恢复 UART GPIO字段默认值。 |
| `doc/README.md` | 更新当前状态及 MAX3490 接线设计。 |

## 最终配置与接线

- UART控制器：UART1。
- ESP32 GPIO43/UART1 TX → MAX3490 DI（3脚）。
- ESP32 GPIO44/UART1 RX ← MAX3490 RO（2脚）。
- MAX3490 VCC（1脚）接3.3V，GND（4脚）接ESP32 GND。
- 串口参数保持115200-8-N-1，无硬件流控。
- USB Serial/JTAG 继续输出启动状态和 UART RX 数据日志。

MAX3490只做接收时，A（8脚）和B（7脚）连接RS485总线，Y/Z可不连接；GPIO43到DI的连接可以保留，不影响独立的A/B接收通道。

## 架构影响

仅恢复集成工程传给 `uart_pt` 的GPIO参数，不修改桥接业务及 `uart_pt`、`wifi_module` 公共组件。

## 构建与验证

已使用 `scripts/build.ps1` 完成ESP-IDF v6.1、ESP32-S3全量构建，有效配置确认为UART1、GPIO43/TX、GPIO44/RX。烧录后启动日志应显示 `Bridge started: UART1 TX=43 RX=44`；实际RS485接收需连接硬件验证。
