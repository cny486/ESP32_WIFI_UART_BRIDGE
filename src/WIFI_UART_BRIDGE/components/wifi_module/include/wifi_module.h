/**
 * @file wifi_module.h
 * @brief 可移植的 DSS 上位机 Wi-Fi/TCP/UDP ESP-IDF 组件公共接口。
 *
 * 状态仅由模块内部 wifi_state_task 写入；其它任务通过事件队列更新状态，
 * 调用方通过 wifi_module_get_state() 获取一致性快照。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define WIFI_MODULE_AT_FRAME_SIZE 19U
#define WIFI_MODULE_MAX_RX_SIZE   6144U

typedef enum {
    WIFI_MODULE_PROTOCOL_RAW = 0,
    WIFI_MODULE_PROTOCOL_ACK,
    WIFI_MODULE_PROTOCOL_AT,
    WIFI_MODULE_PROTOCOL_TIME,
    WIFI_MODULE_PROTOCOL_CFG,
    WIFI_MODULE_PROTOCOL_UUID,
    WIFI_MODULE_PROTOCOL_OTA,
} wifi_module_protocol_t;

typedef enum {
    WIFI_MODULE_EVENT_WIFI_STARTED,
    WIFI_MODULE_EVENT_WIFI_CONNECTED,
    WIFI_MODULE_EVENT_WIFI_DISCONNECTED,
    WIFI_MODULE_EVENT_IP_ACQUIRED,
    WIFI_MODULE_EVENT_TCP_CONNECTED,
    WIFI_MODULE_EVENT_TCP_DISCONNECTED,
    WIFI_MODULE_EVENT_TCP_RETRY_EXHAUSTED,
    WIFI_MODULE_EVENT_UDP_LISTENING,
    WIFI_MODULE_EVENT_UDP_TCP_RESTART,
} wifi_module_event_t;

typedef struct {
    bool initialized;
    bool wifi_enabled;
    bool wifi_connected;
    bool ip_acquired;
    bool tcp_requested;
    bool tcp_connected;
    bool udp_listening;
    uint8_t wifi_retry_count;
    uint8_t tcp_retry_count;
    uint32_t state_revision;
} wifi_module_state_t;

typedef struct {
    char ssid[33];
    char password[65];
    char tcp_host[46];
    uint16_t tcp_port;
    uint16_t udp_listen_port;
    uint8_t wifi_max_retries;
    uint8_t tcp_max_retries;
    uint32_t tcp_retry_delay_ms;
} wifi_module_config_t;

typedef struct { uint8_t command[4]; uint64_t parameter; } wifi_module_at_command_t;
typedef void (*wifi_module_event_cb_t)(wifi_module_event_t event, const wifi_module_state_t *state, void *user_ctx);
typedef void (*wifi_module_protocol_cb_t)(wifi_module_protocol_t protocol, const uint8_t *data, size_t length, void *user_ctx);
typedef void (*wifi_module_at_cb_t)(const wifi_module_at_command_t *command, void *user_ctx);

typedef struct {
    wifi_module_event_cb_t event_cb;
    wifi_module_protocol_cb_t protocol_cb;
    wifi_module_at_cb_t at_command_cb;
    void *user_ctx;
} wifi_module_callbacks_t;

/* 初始化全部资源并自动连接 Wi-Fi；获得 IP 后自动建立双 TCP 任务。 */
esp_err_t wifi_module_init(const wifi_module_config_t *config, const wifi_module_callbacks_t *callbacks);
/* 成熟项目 add_to_tcp_queue() 的模块化 TCP 上传接口，数据在调用内完成拷贝。 */
esp_err_t wifi_module_add_to_tcp_queue(const void *data, size_t length, TickType_t timeout);
esp_err_t wifi_module_send(const void *data, size_t length, TickType_t timeout);
esp_err_t wifi_module_tcp_start(void);
esp_err_t wifi_module_tcp_stop(void);
esp_err_t wifi_module_tcp_restart(void);
esp_err_t wifi_module_set_wifi_config(const char *ssid, const char *password);
esp_err_t wifi_module_get_state(wifi_module_state_t *snapshot);
esp_err_t wifi_module_parse_at_frame(const uint8_t *frame, size_t length, wifi_module_at_command_t *command);

