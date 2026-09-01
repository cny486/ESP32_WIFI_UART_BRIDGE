# uart_pt ESP-IDF component

`uart_pt` 是可移植的双向 UART 透传组件。它把 UART 收到的数据分块投递给上层端点，支持有限重试和可选的异步确认；上层也可通过线程安全的发送接口向 UART 写入数据。

## 放入其它项目

把完整的 `uart_pt` 文件夹复制到目标 ESP-IDF 工程的 `components/` 目录：

```text
目标工程/
├─ CMakeLists.txt
├─ components/
│  └─ uart_pt/
│     ├─ CMakeLists.txt
│     ├─ README.md
│     ├─ include/
│     │  └─ uart_pt.h
│     └─ uart_pt.c
└─ main/
   ├─ CMakeLists.txt
   └─ main.c
```

ESP-IDF 会自动扫描 `components/uart_pt`，目标工程的顶层 `CMakeLists.txt` 无需修改。在调用方组件中声明依赖：

```cmake
idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES uart_pt
)
```

源码中包含公共头文件：

```c
#include "uart_pt.h"
```

如果不复制组件，也可以在目标工程顶层 `CMakeLists.txt` 的 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 之前加载组件路径：

```cmake
set(EXTRA_COMPONENT_DIRS "D:/work/esp_driver/UART_T_T/.component/uart_pt")
```

## 最小使用示例

```c
#include "esp_err.h"
#include "uart_pt.h"

static uart_pt_submit_result_t on_uart_data(const uart_pt_message_t *message,
                                            void *context)
{
    (void)context;
    /* message->data / message->length 是本次收到的 UART 数据块。 */
    return UART_PT_SUBMIT_ACCEPTED;
}

void app_main(void)
{
    uart_pt_config_t config = UART_PT_DEFAULT_CONFIG();
    config.port = UART_NUM_1;
    config.tx_pin = GPIO_NUM_17;
    config.rx_pin = GPIO_NUM_18;
    config.baud_rate = 115200;

    ESP_ERROR_CHECK(uart_pt_init(&config));
    ESP_ERROR_CHECK(uart_pt_bind_endpoint(on_uart_data, NULL));
    ESP_ERROR_CHECK(uart_pt_start());
}
```

默认配置使用 UART1、GPIO17 TX、GPIO18 RX、115200-8-N-1，无硬件流控。集成时应根据目标硬件修改引脚和串口参数。

## 数据方向

```text
UART RX -> 接收任务 -> 固定内存块 -> 上行端点回调
业务数据 -> uart_pt_send() -> UART TX
```

单个上行块最大为 `UART_PT_MAX_PAYLOAD_SIZE`（256）字节。内部最多管理 `UART_PT_MAX_BLOCKS`（16）个固定块，实际数量由 `config.block_count` 决定，不在接收路径动态分配数据块。

## 配置字段

`uart_pt_config_t` 的主要字段：

- `port`、`tx_pin`、`rx_pin`、`rts_pin`、`cts_pin`：UART 控制器和引脚。
- `baud_rate`、`data_bits`、`parity`、`stop_bits`：串口帧格式。
- `flow_control`、`flow_control_threshold`：硬件流控设置。
- `rx_buffer_size`、`event_queue_size`：ESP-IDF UART 驱动接收缓冲和事件队列大小，必须非零。
- `block_count`：组件上行固定块数量，范围为 1 到 `UART_PT_MAX_BLOCKS`。
- `retry_limit`：端点忙、离线或确认失败时的最大重试次数。
- `default_message_flags`：接收消息默认标志；设置 `UART_PT_FLAG_REQUIRE_CONFIRM` 会启用异步确认。
- `confirm_timeout_ms`：可靠模式等待确认的时间。
- `tx_timeout_ms`：等待 UART 硬件发送完成的时间。

## 公共函数

### `uart_pt_init`

```c
esp_err_t uart_pt_init(const uart_pt_config_t *config);
```

复制配置，创建锁、队列和固定消息块，并配置、安装 ESP-IDF UART 驱动。组件只能初始化一次；成功初始化后配置对象可释放或复用。

### `uart_pt_bind_endpoint`

```c
esp_err_t uart_pt_bind_endpoint(uart_pt_submit_fn_t submit, void *context);
```

绑定 UART RX 数据的上行回调及用户上下文。可在运行期间重新绑定；传入 `NULL` 可使端点进入离线状态。

端点返回值决定消息处理：

- `UART_PT_SUBMIT_ACCEPTED`：普通消息处理完成并释放；可靠消息进入等待确认状态。
- `UART_PT_SUBMIT_BUSY` 或 `UART_PT_SUBMIT_OFFLINE`：按 `retry_limit` 重新投递，耗尽后丢弃。
- `UART_PT_SUBMIT_REJECTED`：立即丢弃，不再重试。

`message` 指针属于组件，不得由回调释放或在回调外继续访问；需要异步处理数据时，应复制必要字段和数据。

### `uart_pt_start` / `uart_pt_stop` / `uart_pt_is_running`

```c
esp_err_t uart_pt_start(void);
esp_err_t uart_pt_stop(void);
bool uart_pt_is_running(void);
```

`uart_pt_start()` 创建接收任务和上行任务，重复启动会直接返回成功。`uart_pt_stop()` 停止这两个任务，但保留 UART 驱动和组件资源，之后可再次调用 `uart_pt_start()`。`uart_pt_is_running()` 返回当前运行标志。

### `uart_pt_send`

```c
esp_err_t uart_pt_send(const uint8_t *data,
                       size_t length,
                       uint32_t timeout_ms);
```

向 UART TX 发送数据。组件用互斥锁串行化多个调用方；`timeout_ms` 是等待发送锁的时间，获得锁后的硬件发送完成等待使用 `config.tx_timeout_ms`。成功返回 `ESP_OK`，锁超时返回 `ESP_ERR_TIMEOUT`，写入或等待发送失败返回 `ESP_FAIL`。

```c
static const uint8_t reply[] = "OK\r\n";
ESP_ERROR_CHECK(uart_pt_send(reply, sizeof(reply) - 1U, 100));
```

### `uart_pt_report_result`

```c
esp_err_t uart_pt_report_result(uint32_t message_id,
                                uart_pt_confirm_result_t result);
```

仅用于带 `UART_PT_FLAG_REQUIRE_CONFIRM` 的可靠消息。业务层完成异步处理后，以回调收到的 `message->id` 报告结果：

- `UART_PT_CONFIRM_OK`：确认成功并释放消息块。
- `UART_PT_CONFIRM_RETRY`：按重试限制重新投递。
- `UART_PT_CONFIRM_DROP`：释放并丢弃。

找不到仍在等待确认的消息时返回 `ESP_ERR_NOT_FOUND`。

### `uart_pt_get_stats`

```c
void uart_pt_get_stats(uart_pt_stats_t *stats);
```

在线程安全的锁保护下复制累计统计，包括 RX 消息和字节、缓冲溢出、上行丢弃与重试、确认超时、TX 消息与失败等。

### `uart_pt_test_inject_rx`

```c
esp_err_t uart_pt_test_inject_rx(const uint8_t *data, size_t length);
```

测试专用接口：绕过 UART 硬件，把数据按最多 256 字节的块注入上行队列。必须在初始化并启动后调用；产品业务不应依赖该接口代替真实 UART RX。

## 生命周期与并发约束

- 当前组件为单实例，只能管理一组 UART 配置。
- 当前没有反初始化接口；`uart_pt_stop()` 不卸载 UART 驱动，也不释放组件资源。
- 上行回调在组件的 `uart_pt_up` 任务中同步执行，不应长时间阻塞。
- `uart_pt_send()` 支持多任务调用，并通过内部互斥锁保证单次 UART 写入不互相穿插。
- 使用可靠确认时，业务层必须及时调用 `uart_pt_report_result()`；否则会在 `confirm_timeout_ms` 后自动重试或丢弃。

## 依赖与验证范围

组件 CMake 已声明 `esp_common`、`esp_driver_gpio`、`esp_driver_uart` 和 `freertos` 依赖，调用方不需要重复声明。当前在 ESP-IDF v6.1、ESP32-S3 目标上通过 UART_T_T Demo 工程和内置组件版 WIFI_UART_BRIDGE 工程编译验证；UART 引脚、电平、波特率和外设收发仍需烧录后验证。
