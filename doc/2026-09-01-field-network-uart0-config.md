# 现场 Wi-Fi、TCP 与 UART0 透传配置

## 本次目标

为 `WIFI_UART_BRIDGE` 写入指定的 Wi-Fi、TCP 服务器和 UART0 接线参数，并明确把 Wi-Fi TCP 上传函数配置为 UART 透传的上行端点。

## 涉及文件

| 文件 | 修改内容 |
| --- | --- |
| `src/WIFI_UART_BRIDGE/sdkconfig.defaults` | 保存 Wi-Fi、TCP、UART0 和 USB Serial/JTAG 控制台的项目默认配置。 |
| `src/WIFI_UART_BRIDGE/sdkconfig` | 更新当前构建使用的同组配置。 |
| `src/WIFI_UART_BRIDGE/main/Kconfig.projbuild` | 更新新配置生成时的字段默认值。 |
| `src/WIFI_UART_BRIDGE/main/main.c` | 将 UART 上行适配端点命名为 `wifi_upload_uart_endpoint()`，并继续在端点内调用 Wi-Fi 上传队列函数。 |
| `doc/README.md` | 更新当前状态、接线、参数和透传设计说明。 |

## 参数与接线

- Wi-Fi SSID 已按指定值设置，密码保存在项目配置中。
- TCP 服务器 IPv4 地址为 `192.168.137.201`，端口保持 `54321`。
- UART 控制器改为 UART0，保持 115200-8-N-1、无硬件流控。
- ESP32-S3 原生 `RXD0` 为 GPIO44，连接对端的 `UART0_MISO`。
- ESP32-S3 原生 `TXD0` 为 GPIO43，连接对端的 `UART0_MOSI`。
- 两端必须共地。

原项目使用 UART0 作为主日志控制台，会造成日志输出和透传 TX 共用 GPIO43。现已将主控制台切换到 ESP32-S3 USB Serial/JTAG，并禁用次控制台，从而将 UART0 独占给透传组件。

## Wi-Fi 上传作为 UART 透传端点

`uart_pt_bind_endpoint(wifi_upload_uart_endpoint, NULL)` 把 UART RX 上行绑定到适配端点。由于两套组件的函数签名不同，不能直接把 `wifi_module_add_to_tcp_queue()` 当作 UART 回调；适配端点从 `uart_pt_message_t` 取出 `data` 和 `length`，调用：

```c
wifi_module_add_to_tcp_queue(message->data, message->length, 0);
```

入队成功时返回 `UART_PT_SUBMIT_ACCEPTED`；TCP 上传队列忙或内存不足时返回 `UART_PT_SUBMIT_BUSY`；Wi-Fi 模块不可用时返回 `UART_PT_SUBMIT_OFFLINE`，由 UART 组件按配置执行重试。

## 构建与验证

已使用 `scripts/build.ps1` 完成 ESP-IDF v6.1、ESP32-S3 全量构建，结果如下：

- Wi-Fi SSID、密码、TCP 地址与端口均已进入有效 `sdkconfig`。
- UART0、GPIO43 TXD0、GPIO44 RXD0 和 USB Serial/JTAG 主控制台配置均已生效。
- `libwifi_module.a`、`libuart_pt.a` 与 `wifi_upload_uart_endpoint()` 共同编译链接通过。
- 生成的 `WIFI_UART_BRIDGE.bin` 为 `0xc65b0` 字节（812464 字节），默认 app 分区剩余 23%。

实际无线连接、TCP 服务器可达性和 UART0 电气收发仍需烧录验证。
