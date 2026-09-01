# 交换 UART1 TX/RX GPIO

## 本次目标

保持 UART1 控制器不变，将透传引脚从 GPIO43/TX、GPIO44/RX 交换为 GPIO44/TX、GPIO43/RX，用于现场接线调试。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 默认 TX 改为 GPIO44，RX 改为 GPIO43。 |
| `src/WIFI_UART_BRIDGE/sdkconfig` | 同步当前构建的有效 TX/RX 配置。 |
| `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild` | 更新新配置生成时的 UART GPIO 默认值。 |
| `doc/README.md` | 更新当前状态和 UART1 接线设计。 |

## 最终配置与接线

- UART控制器：UART1。
- ESP32-S3 TX：GPIO44，连接对端 RX。
- ESP32-S3 RX：GPIO43，连接对端 TX。
- 串口参数：115200-8-N-1，无硬件流控。
- 两端必须共地并使用 3.3 V 逻辑电平。
- USB Serial/JTAG 继续输出启动状态和 UART RX 数据日志。

## 架构影响

仅交换集成工程传给 `uart_pt` 的 TX/RX GPIO 参数，不修改桥接业务及 `uart_pt`、`wifi_module` 公共组件。

## 构建与验证

已使用 `scripts/build.ps1` 完成 ESP-IDF v6.1、ESP32-S3 全量构建，有效配置确认为 UART1、GPIO44/TX、GPIO43/RX；生成的 `WIFI_UART_BRIDGE.bin` 为 `0xc6930` 字节（813360 字节），默认 app 分区剩余 22%。

烧录后启动日志应显示 `Bridge started: UART1 TX=44 RX=43`，实际收发需连接硬件验证。
