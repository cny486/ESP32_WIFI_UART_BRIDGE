# UART1 RX切换至GPIO43

## 本次目标

现场已确认MAX3490 RO存在接收波形且连接ESP32 GPIO43，因此将UART1 RX切换至GPIO43；同时将UART1 TX设置为连接MAX3490 DI的GPIO44。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 设置UART1 TX=GPIO44、RX=GPIO43。 |
| `src/WIFI_UART_BRIDGE/sdkconfig` | 同步当前构建使用的有效配置。 |
| `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild` | 更新UART GPIO默认值。 |
| `doc/README.md` | 更新当前状态及MAX3490最终信号路由。 |

## 最终配置与接线

- ESP32 GPIO44/UART1 TX → MAX3490 DI（3脚）。
- ESP32 GPIO43/UART1 RX ← MAX3490 RO（2脚）。
- UART参数保持115200-8-N-1，无硬件流控。
- USB Serial/JTAG继续输出启动配置和UART RX数据日志。

## 架构影响

仅修改集成工程传给 `uart_pt` 的TX/RX GPIO参数，不修改UART组件、Wi-Fi组件或透传业务。

## 构建与验证

已使用 `scripts/build.ps1` 完成ESP-IDF v6.1、ESP32-S3全量构建，有效配置确认为UART1、GPIO44/TX、GPIO43/RX。生成的 `WIFI_UART_BRIDGE.bin` 为 `0xc6c60` 字节（814176字节），默认app分区剩余22%。

烧录后启动配置应显示 `UART1, TX=GPIO44, RX=GPIO43`；发送RS485数据时应出现 `UART RX` 日志，实际接收待硬件验证。
