/**
 * @file uart_pt.h
 * @brief 可移植的双向 UART 透传 ESP-IDF 组件公共接口。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_PT_MAX_PAYLOAD_SIZE 256U
#define UART_PT_MAX_BLOCKS 16U

typedef struct {
    uint32_t id;
    size_t length;
    uint8_t retry_count;
    uint8_t retry_limit;
    uint32_t flags;
    uint8_t data[UART_PT_MAX_PAYLOAD_SIZE];
} uart_pt_message_t;

enum { UART_PT_FLAG_REQUIRE_CONFIRM = (1U << 0) };
typedef enum { UART_PT_SUBMIT_ACCEPTED, UART_PT_SUBMIT_BUSY, UART_PT_SUBMIT_OFFLINE, UART_PT_SUBMIT_REJECTED } uart_pt_submit_result_t;
typedef enum { UART_PT_CONFIRM_OK, UART_PT_CONFIRM_RETRY, UART_PT_CONFIRM_DROP } uart_pt_confirm_result_t;
typedef uart_pt_submit_result_t (*uart_pt_submit_fn_t)(const uart_pt_message_t *message, void *context);

typedef struct {
    uart_port_t port; int tx_pin; int rx_pin; int rts_pin; int cts_pin; int baud_rate;
    uart_word_length_t data_bits; uart_parity_t parity; uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_control; uint8_t flow_control_threshold;
    size_t rx_buffer_size; size_t event_queue_size; size_t block_count; uint8_t retry_limit;
    /** Applied to received messages; set UART_PT_FLAG_REQUIRE_CONFIRM for reliable mode. */
    uint32_t default_message_flags;
    uint32_t confirm_timeout_ms; uint32_t tx_timeout_ms;
} uart_pt_config_t;

#define UART_PT_DEFAULT_CONFIG() ((uart_pt_config_t){ .port = UART_NUM_1, .tx_pin = GPIO_NUM_17, .rx_pin = GPIO_NUM_18, .rts_pin = UART_PIN_NO_CHANGE, .cts_pin = UART_PIN_NO_CHANGE, .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1, .flow_control = UART_HW_FLOWCTRL_DISABLE, .flow_control_threshold = 0, .rx_buffer_size = 2048, .event_queue_size = 20, .block_count = 8, .retry_limit = 2, .default_message_flags = 0, .confirm_timeout_ms = 5000, .tx_timeout_ms = 1000 })

typedef struct {
    uint32_t rx_messages, rx_bytes, rx_dropped_no_block, rx_dropped_queue_full;
    uint32_t rx_fifo_overflow, rx_buffer_full, upstream_dropped, upstream_retries;
    uint32_t confirm_timeouts, tx_messages, tx_failures, tx_busy;
} uart_pt_stats_t;

esp_err_t uart_pt_init(const uart_pt_config_t *config);
esp_err_t uart_pt_start(void);
esp_err_t uart_pt_stop(void);
bool uart_pt_is_running(void);
esp_err_t uart_pt_bind_endpoint(uart_pt_submit_fn_t submit, void *context);
esp_err_t uart_pt_send(const uint8_t *data, size_t length, uint32_t timeout_ms);
esp_err_t uart_pt_report_result(uint32_t message_id, uart_pt_confirm_result_t result);
void uart_pt_get_stats(uart_pt_stats_t *stats);
/** Test-only helper: injects bytes into the internal upstream path without UART hardware. */
esp_err_t uart_pt_test_inject_rx(const uint8_t *data, size_t length);
#ifdef __cplusplus
}
#endif

