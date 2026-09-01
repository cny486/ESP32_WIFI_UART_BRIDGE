# wifi_module ESP-IDF component

`wifi_module` 是从 WIFI_MODE_DSS 示例工程拆出的可移植 ESP-IDF 组件，提供 Wi-Fi STA、TCP 收发队列、断线重连、UDP `TCPCONNECT` 唤醒和 DSS 下行协议分流。

## 放入其它项目

把完整的 `wifi_module` 文件夹复制到目标 ESP-IDF 工程的 `components/` 目录：

```text
目标工程/
├─ CMakeLists.txt
├─ components/
│  └─ wifi_module/
│     ├─ CMakeLists.txt
│     ├─ README.md
│     ├─ include/
│     │  └─ wifi_module.h
│     └─ wifi_module.c
└─ main/
   ├─ CMakeLists.txt
   └─ main.c
```

ESP-IDF 会自动扫描 `components/wifi_module`，目标工程的顶层 `CMakeLists.txt` 无需修改。在调用方组件中声明依赖：

```cmake
idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES wifi_module
)
```

然后包含公共头文件：

```c
#include "wifi_module.h"
```

若不复制组件，也可以在目标工程顶层 `CMakeLists.txt` 的 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 之前添加组件的绝对路径或相对路径：

```cmake
set(EXTRA_COMPONENT_DIRS "D:/work/esp_driver/WIFI_MODE_DSS/.component/wifi_module")
```

## 最小初始化示例

```c
#include "esp_err.h"
#include "wifi_module.h"

static void on_event(wifi_module_event_t event,
                     const wifi_module_state_t *state,
                     void *user_ctx)
{
    (void)event;
    (void)state;
    (void)user_ctx;
}

void app_main(void)
{
    static const wifi_module_config_t config = {
        .ssid = "your-ssid",
        .password = "your-password",
        .tcp_host = "192.168.1.100",
        .tcp_port = 54321,
        .udp_listen_port = 12345,
        .wifi_max_retries = 5,
        .tcp_max_retries = 5,
        .tcp_retry_delay_ms = 1000,
    };
    static const wifi_module_callbacks_t callbacks = {
        .event_cb = on_event,
    };

    ESP_ERROR_CHECK(wifi_module_init(&config, &callbacks));
}
```

`wifi_module_init()` 会初始化 NVS、默认 netif、默认事件循环和 Wi-Fi STA，并立即开始连接。当前组件只允许初始化一次，且没有反初始化接口；集成时应由它负责首次网络基础设施初始化。

## 公共函数

### `wifi_module_init`

```c
esp_err_t wifi_module_init(const wifi_module_config_t *config,
                           const wifi_module_callbacks_t *callbacks);
```

创建模块资源并启动 Wi-Fi。`config` 必须提供非空 SSID、TCP IPv4 地址和非零 TCP 端口；`callbacks` 可为 `NULL`。成功返回 `ESP_OK`。

### `wifi_module_add_to_tcp_queue` / `wifi_module_send`

```c
esp_err_t wifi_module_add_to_tcp_queue(const void *data,
                                       size_t length,
                                       TickType_t timeout);
esp_err_t wifi_module_send(const void *data,
                           size_t length,
                           TickType_t timeout);
```

把数据复制到内部 TCP 发送队列，调用返回后原始缓冲区即可释放或复用。`wifi_module_send()` 是同一功能的简写别名。TCP 尚未连接时数据会留在队列中；队列超时返回 `ESP_ERR_TIMEOUT`。

```c
static const char message[] = "HELLO";
ESP_ERROR_CHECK(wifi_module_send(message, sizeof(message) - 1,
                                 pdMS_TO_TICKS(100)));
```

### TCP 控制函数

```c
esp_err_t wifi_module_tcp_start(void);
esp_err_t wifi_module_tcp_stop(void);
esp_err_t wifi_module_tcp_restart(void);
```

- `wifi_module_tcp_start()`：请求创建或继续 TCP 收发任务。
- `wifi_module_tcp_stop()`：停止 TCP 请求、关闭当前 socket，并在配置了 UDP 端口时进入 UDP 监听。
- `wifi_module_tcp_restart()`：关闭当前 socket，重新请求 TCP 连接。

这些函数必须在 `wifi_module_init()` 成功后调用，否则返回 `ESP_ERR_INVALID_STATE`。

### `wifi_module_set_wifi_config`

```c
esp_err_t wifi_module_set_wifi_config(const char *ssid,
                                      const char *password);
```

运行时更新 STA 凭据并重新连接。SSID 不得为空；函数会立即复制字符串。

### `wifi_module_get_state`

```c
esp_err_t wifi_module_get_state(wifi_module_state_t *snapshot);
```

在线程安全的锁保护下复制当前状态快照，可检查 Wi-Fi、IP、TCP、UDP 状态、重试次数和 `state_revision`。

```c
wifi_module_state_t state;
if (wifi_module_get_state(&state) == ESP_OK && state.tcp_connected) {
    /* TCP 已连接 */
}
```

### `wifi_module_parse_at_frame`

```c
esp_err_t wifi_module_parse_at_frame(const uint8_t *frame,
                                     size_t length,
                                     wifi_module_at_command_t *command);
```

解析固定 19 字节 DSS AT 帧。合法帧必须以 `AT+\0` 开始、以 `EAT` 结束；解析结果包含 4 字节命令和按小端序读取的 64 位参数。

## 回调引用

`wifi_module_callbacks_t` 支持三类可选回调：

- `event_cb`：Wi-Fi、IP、TCP、UDP 状态事件；回调中的状态指针只在本次调用期间有效，需要长期保存时应自行复制。
- `protocol_cb`：ACK、AT、TIME、CFG、UUID、OTA 或 RAW 下行数据；数据指针只在回调期间有效。
- `at_command_cb`：合法 AT 帧的结构化命令和参数。
- `user_ctx`：原样传回所有回调，供调用方关联对象或上下文。

回调由模块内部任务同步执行，不应在回调里长时间阻塞；需要耗时处理时，应复制必要数据后投递给业务任务。

## 依赖与验证范围

组件 CMake 已声明 `esp_common`、`freertos`、`esp_wifi`、`esp_event`、`esp_netif`、`nvs_flash` 和 `lwip` 依赖，不需要调用方重复声明。当前在 ESP-IDF v6.1、ESP32-S3 目标上通过 WIFI_MODE_DSS 示例工程和内置组件版 WIFI_UART_BRIDGE 工程编译验证；硬件联网与服务器通信仍需烧录后验证。
