/**
 * @file uart_pt.c
 * @brief UART 接收分块、上行投递、可靠确认和下行发送实现。
 */
#include "uart_pt.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TASK_STACK 3072
#define TASK_PRIO  8

typedef enum { BLOCK_FREE, BLOCK_QUEUED, BLOCK_WAIT_CONFIRM } block_state_t;
typedef struct { uart_pt_message_t msg; block_state_t state; TickType_t deadline; } block_t;
typedef struct {
    uart_pt_config_t config;
    QueueHandle_t events, free_queue, upstream_queue;
    SemaphoreHandle_t lock, tx_lock;
    TaskHandle_t rx_task, up_task;
    block_t blocks[UART_PT_MAX_BLOCKS];
    uart_pt_submit_fn_t endpoint;
    void *endpoint_context;
    uint32_t next_id;
    bool initialized, running;
    uart_pt_stats_t stats;
} context_t;
static context_t s_ctx;

static void stat_inc(uint32_t *stat) { xSemaphoreTake(s_ctx.lock, portMAX_DELAY); ++*stat; xSemaphoreGive(s_ctx.lock); }
static void release_block(uint8_t index) { s_ctx.blocks[index].state = BLOCK_FREE; (void)xQueueSend(s_ctx.free_queue, &index, 0); }
static bool enqueue_block(uint8_t index)
{
    s_ctx.blocks[index].state = BLOCK_QUEUED;
    if (xQueueSend(s_ctx.upstream_queue, &index, 0) == pdPASS) return true;
    stat_inc(&s_ctx.stats.rx_dropped_queue_full); release_block(index); return false;
}
static void retry_or_release(uint8_t index)
{
    block_t *block = &s_ctx.blocks[index];
    if (block->msg.retry_count < block->msg.retry_limit) {
        ++block->msg.retry_count; stat_inc(&s_ctx.stats.upstream_retries); (void)enqueue_block(index);
    } else { stat_inc(&s_ctx.stats.upstream_dropped); release_block(index); }
}

static void receive_task(void *arg)
{
    uart_event_t event;
    while (s_ctx.running && xQueueReceive(s_ctx.events, &event, portMAX_DELAY) == pdPASS) {
        if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining && s_ctx.running) {
                uint8_t index;
                if (xQueueReceive(s_ctx.free_queue, &index, 0) != pdPASS) {
                    uint8_t discard[64]; size_t count = remaining > sizeof(discard) ? sizeof(discard) : remaining;
                    int read = uart_read_bytes(s_ctx.config.port, discard, count, 0);
                    stat_inc(&s_ctx.stats.rx_dropped_no_block);
                    if (read <= 0) {
                        break;
                    }
                    remaining -= (size_t)read;
                    continue;
                }
                block_t *block = &s_ctx.blocks[index];
                size_t count = remaining > UART_PT_MAX_PAYLOAD_SIZE ? UART_PT_MAX_PAYLOAD_SIZE : remaining;
                int read = uart_read_bytes(s_ctx.config.port, block->msg.data, count, pdMS_TO_TICKS(20));
                if (read <= 0) { release_block(index); break; }
                block->msg.id = ++s_ctx.next_id; block->msg.length = (size_t)read;
                block->msg.retry_count = 0; block->msg.retry_limit = s_ctx.config.retry_limit; block->msg.flags = s_ctx.config.default_message_flags;
                xSemaphoreTake(s_ctx.lock, portMAX_DELAY); ++s_ctx.stats.rx_messages; s_ctx.stats.rx_bytes += (uint32_t)read; xSemaphoreGive(s_ctx.lock);
                (void)enqueue_block(index); remaining -= (size_t)read;
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            stat_inc(event.type == UART_FIFO_OVF ? &s_ctx.stats.rx_fifo_overflow : &s_ctx.stats.rx_buffer_full);
            uart_flush_input(s_ctx.config.port); xQueueReset(s_ctx.events);
        }
    }
    vTaskDelete(NULL);
}

static void expire_confirmations(void)
{
    TickType_t now = xTaskGetTickCount();
    for (uint8_t i = 0; i < s_ctx.config.block_count; ++i) {
        if (s_ctx.blocks[i].state == BLOCK_WAIT_CONFIRM && (int32_t)(now - s_ctx.blocks[i].deadline) >= 0) {
            stat_inc(&s_ctx.stats.confirm_timeouts); retry_or_release(i);
        }
    }
}
static void upstream_task(void *arg)
{
    uint8_t index;
    while (s_ctx.running) {
        if (xQueueReceive(s_ctx.upstream_queue, &index, pdMS_TO_TICKS(100)) == pdPASS) {
            uart_pt_submit_fn_t endpoint; void *endpoint_context;
            xSemaphoreTake(s_ctx.lock, portMAX_DELAY); endpoint = s_ctx.endpoint; endpoint_context = s_ctx.endpoint_context; xSemaphoreGive(s_ctx.lock);
            uart_pt_submit_result_t result = endpoint ? endpoint(&s_ctx.blocks[index].msg, endpoint_context) : UART_PT_SUBMIT_OFFLINE;
            if (result == UART_PT_SUBMIT_ACCEPTED) {
                if (s_ctx.blocks[index].msg.flags & UART_PT_FLAG_REQUIRE_CONFIRM) {
                    s_ctx.blocks[index].state = BLOCK_WAIT_CONFIRM;
                    s_ctx.blocks[index].deadline = xTaskGetTickCount() + pdMS_TO_TICKS(s_ctx.config.confirm_timeout_ms);
                } else release_block(index);
            } else if (result == UART_PT_SUBMIT_REJECTED) { stat_inc(&s_ctx.stats.upstream_dropped); release_block(index); }
            else retry_or_release(index);
        }
        expire_confirmations();
    }
    vTaskDelete(NULL);
}

esp_err_t uart_pt_init(const uart_pt_config_t *config)
{
    if (!config || config->block_count == 0 || config->block_count > UART_PT_MAX_BLOCKS || !config->rx_buffer_size || !config->event_queue_size) return ESP_ERR_INVALID_ARG;
    if (s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    memset(&s_ctx, 0, sizeof(s_ctx)); s_ctx.config = *config;
    s_ctx.lock = xSemaphoreCreateMutex(); s_ctx.tx_lock = xSemaphoreCreateMutex();
    s_ctx.free_queue = xQueueCreate(config->block_count, sizeof(uint8_t)); s_ctx.upstream_queue = xQueueCreate(config->block_count, sizeof(uint8_t));
    if (!s_ctx.lock || !s_ctx.tx_lock || !s_ctx.free_queue || !s_ctx.upstream_queue) return ESP_ERR_NO_MEM;
    uart_config_t driver_config = {.baud_rate = config->baud_rate, .data_bits = config->data_bits, .parity = config->parity, .stop_bits = config->stop_bits, .flow_ctrl = config->flow_control, .rx_flow_ctrl_thresh = config->flow_control_threshold, .source_clk = UART_SCLK_DEFAULT};
    esp_err_t err = uart_param_config(config->port, &driver_config);
    if (err == ESP_OK) err = uart_set_pin(config->port, config->tx_pin, config->rx_pin, config->rts_pin, config->cts_pin);
    if (err == ESP_OK) err = uart_driver_install(config->port, config->rx_buffer_size, 0, config->event_queue_size, &s_ctx.events, 0);
    if (err != ESP_OK) return err;
    for (uint8_t i = 0; i < config->block_count; ++i) { s_ctx.blocks[i].state = BLOCK_FREE; (void)xQueueSend(s_ctx.free_queue, &i, 0); }
    s_ctx.initialized = true; return ESP_OK;
}
esp_err_t uart_pt_start(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (s_ctx.running) return ESP_OK;
    s_ctx.running = true;
    if (xTaskCreate(receive_task, "uart_pt_rx", TASK_STACK, NULL, TASK_PRIO, &s_ctx.rx_task) != pdPASS || xTaskCreate(upstream_task, "uart_pt_up", TASK_STACK, NULL, TASK_PRIO, &s_ctx.up_task) != pdPASS) { s_ctx.running = false; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
esp_err_t uart_pt_stop(void) { if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE; s_ctx.running = false; if (s_ctx.rx_task) { vTaskDelete(s_ctx.rx_task); s_ctx.rx_task = NULL; } if (s_ctx.up_task) { vTaskDelete(s_ctx.up_task); s_ctx.up_task = NULL; } return ESP_OK; }
bool uart_pt_is_running(void) { return s_ctx.running; }
esp_err_t uart_pt_bind_endpoint(uart_pt_submit_fn_t submit, void *context) { if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE; xSemaphoreTake(s_ctx.lock, portMAX_DELAY); s_ctx.endpoint = submit; s_ctx.endpoint_context = context; xSemaphoreGive(s_ctx.lock); return ESP_OK; }
esp_err_t uart_pt_send(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    if (!s_ctx.initialized || !data || !length) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.tx_lock, pdMS_TO_TICKS(timeout_ms)) != pdPASS) { stat_inc(&s_ctx.stats.tx_busy); return ESP_ERR_TIMEOUT; }
    int written = uart_write_bytes(s_ctx.config.port, data, length);
    esp_err_t result = (written == (int)length && uart_wait_tx_done(s_ctx.config.port, pdMS_TO_TICKS(s_ctx.config.tx_timeout_ms)) == ESP_OK) ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(s_ctx.tx_lock); stat_inc(result == ESP_OK ? &s_ctx.stats.tx_messages : &s_ctx.stats.tx_failures); return result;
}
esp_err_t uart_pt_report_result(uint32_t id, uart_pt_confirm_result_t result)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < s_ctx.config.block_count; ++i) if (s_ctx.blocks[i].state == BLOCK_WAIT_CONFIRM && s_ctx.blocks[i].msg.id == id) { if (result == UART_PT_CONFIRM_RETRY) retry_or_release(i); else release_block(i); return ESP_OK; }
    return ESP_ERR_NOT_FOUND;
}
void uart_pt_get_stats(uart_pt_stats_t *stats) { if (!stats || !s_ctx.lock) return; xSemaphoreTake(s_ctx.lock, portMAX_DELAY); *stats = s_ctx.stats; xSemaphoreGive(s_ctx.lock); }
esp_err_t uart_pt_test_inject_rx(const uint8_t *data, size_t length)
{
    if (!s_ctx.initialized || !s_ctx.running) return ESP_ERR_INVALID_STATE;
    if (!data || !length) return ESP_ERR_INVALID_ARG;
    while (length) {
        uint8_t index;
        if (xQueueReceive(s_ctx.free_queue, &index, 0) != pdPASS) {
            stat_inc(&s_ctx.stats.rx_dropped_no_block);
            return ESP_ERR_NO_MEM;
        }
        block_t *block = &s_ctx.blocks[index];
        size_t count = length > UART_PT_MAX_PAYLOAD_SIZE ? UART_PT_MAX_PAYLOAD_SIZE : length;
        memcpy(block->msg.data, data, count);
        block->msg.id = ++s_ctx.next_id;
        block->msg.length = count;
        block->msg.retry_count = 0;
        block->msg.retry_limit = s_ctx.config.retry_limit;
        block->msg.flags = s_ctx.config.default_message_flags;
        xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
        ++s_ctx.stats.rx_messages;
        s_ctx.stats.rx_bytes += (uint32_t)count;
        xSemaphoreGive(s_ctx.lock);
        (void)enqueue_block(index);
        data += count;
        length -= count;
    }
    return ESP_OK;
}

