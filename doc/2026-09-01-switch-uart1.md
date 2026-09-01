# 透传接口切换到 UART1

## 本次目标

为排查 UART0 无接收问题，将 `WIFI_UART_BRIDGE` 的透传外设切换到 UART1，同时保留已有的 UART RX ASCII/十六进制日志和 Wi-Fi TCP 上传逻辑。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 将默认控制器改为 UART1，保持 TX/RX 为 GPIO43/GPIO44。 |
| `src/WIFI_UART_BRIDGE/sdkconfig` | 同步当前构建使用的有效配置。 |
| `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild` | 更新新配置生成时的 UART 默认值。 |
| `doc/README.md` | 更新当前状态、接线和设计说明。 |

## 配置与接线

- ESP32-S3 UART1 TX：仍为 GPIO43，连接对端接收线 `UART0_MOSI`。
- ESP32-S3 UART1 RX：仍为 GPIO44，连接对端发送线 `UART0_MISO`。
- 两端必须共地，逻辑电平必须为 3.3 V，并保持 115200-8-N-1。
- USB Serial/JTAG 继续作为日志控制台；UART1 只承载透传数据。

ESP32-S3 通过 GPIO Matrix 将 UART1 路由到原 GPIO43/GPIO44，因此现有物理接线无需改变。对端信号名称中的 UART0 只代表对端使用的外设，不要求 ESP32-S3 也使用 UART0。串口连接必须交叉：对端 TX 接 ESP RX，对端 RX 接 ESP TX。

## 架构影响

桥接业务、Wi-Fi 上传端点和两个公共组件均不变，仅修改集成工程传给 `uart_pt` 的控制器与 GPIO 参数。UART RX 日志仍通过 USB Serial/JTAG 输出。

## 构建与验证

已使用 `scripts/build.ps1` 完成 ESP-IDF v6.1、ESP32-S3 全量构建，有效配置确认为 UART1、GPIO43/TX、GPIO44/RX；生成的 `WIFI_UART_BRIDGE.bin` 为 `0xc6930` 字节（813360 字节），默认 app 分区剩余 22%。

烧录后应先观察启动日志中的 `Bridge started: UART1 TX=43 RX=44`，再发送测试数据检查 `UART RX` 日志。实际收发仍需硬件验证。
