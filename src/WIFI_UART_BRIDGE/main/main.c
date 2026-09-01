#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "uart_pt.h"
#include "wifi_module.h"

static const char *TAG = "wifi_uart_bridge";

static void log_uart_rx_message(const uart_pt_message_t *message)
{
    char ascii[UART_PT_MAX_PAYLOAD_SIZE + 1U];

    for (size_t i = 0; i < message->length; ++i) {
        uint8_t byte = message->data[i];
        ascii[i] = (byte >= 0x20U && byte <= 0x7eU) ? (char)byte : '.';
    }
    ascii[message->length] = '\0';

    ESP_LOGI(TAG, "UART RX: id=%u length=%u ASCII=\"%s\"",
             (unsigned)message->id,
             (unsigned)message->length,
             ascii);
    ESP_LOG_BUFFER_HEXDUMP(TAG, message->data, message->length, ESP_LOG_INFO);
}

static uart_pt_submit_result_t wifi_upload_uart_endpoint(const uart_pt_message_t *message,
                                                         void *context)
{
    (void)context;
    if (message->retry_count == 0U) {
        log_uart_rx_message(message);
    }
    esp_err_t err = wifi_module_add_to_tcp_queue(message->data, message->length, 0);
    if (err == ESP_OK) {
        return UART_PT_SUBMIT_ACCEPTED;
    }
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "TCP upload queue busy: %s", esp_err_to_name(err));
        return UART_PT_SUBMIT_BUSY;
    }
    ESP_LOGW(TAG, "TCP upload unavailable: %s", esp_err_to_name(err));
    return UART_PT_SUBMIT_OFFLINE;
}

static void bridge_wifi_to_uart(wifi_module_protocol_t protocol,
                                const uint8_t *data,
                                size_t length,
                                void *context)
{
    (void)context;
    if (!uart_pt_is_running()) {
        ESP_LOGW(TAG, "Drop TCP downlink while UART is stopped: protocol=%d, length=%u",
                 (int)protocol, (unsigned)length);
        return;
    }

    esp_err_t err = uart_pt_send(data, length, CONFIG_BRIDGE_UART_TX_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UART downlink failed: protocol=%d, length=%u, error=%s",
                 (int)protocol, (unsigned)length, esp_err_to_name(err));
    }
}

static void bridge_network_event(wifi_module_event_t event,
                                 const wifi_module_state_t *state,
                                 void *context)
{
    (void)context;
    ESP_LOGI(TAG,
             "Network event=%d revision=%u wifi=%d ip=%d tcp=%d udp=%d",
             (int)event,
             (unsigned)state->state_revision,
             state->wifi_connected,
             state->ip_acquired,
             state->tcp_connected,
             state->udp_listening);
}

static void log_bridge_configuration(void)
{
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  Wi-Fi SSID: %s", CONFIG_BRIDGE_WIFI_SSID);
    ESP_LOGI(TAG, "  Wi-Fi password: %s",
             CONFIG_BRIDGE_WIFI_PASSWORD[0] != '\0' ? "<configured>" : "<empty>");
    ESP_LOGI(TAG, "  TCP server: %s:%d", CONFIG_BRIDGE_TCP_HOST, CONFIG_BRIDGE_TCP_PORT);
    ESP_LOGI(TAG, "  UDP listen port: %d", CONFIG_BRIDGE_UDP_LISTEN_PORT);
    ESP_LOGI(TAG, "  Wi-Fi max retries: %d", CONFIG_BRIDGE_WIFI_MAX_RETRIES);
    ESP_LOGI(TAG, "  TCP max retries: %d, retry delay: %d ms",
             CONFIG_BRIDGE_TCP_MAX_RETRIES,
             CONFIG_BRIDGE_TCP_RETRY_DELAY_MS);
    ESP_LOGI(TAG, "  UART: UART%d, TX=GPIO%d, RX=GPIO%d, baud=%d, 8-N-1",
             CONFIG_BRIDGE_UART_PORT,
             CONFIG_BRIDGE_UART_TX_GPIO,
             CONFIG_BRIDGE_UART_RX_GPIO,
             CONFIG_BRIDGE_UART_BAUD_RATE);
    ESP_LOGI(TAG, "  UART blocks: %d, retries: %d, TX lock timeout: %d ms",
             CONFIG_BRIDGE_UART_BLOCK_COUNT,
             CONFIG_BRIDGE_UART_RETRY_LIMIT,
             CONFIG_BRIDGE_UART_TX_LOCK_TIMEOUT_MS);
    ESP_LOGI(TAG, "  Log console: USB Serial/JTAG");
}

static esp_err_t start_uart_bridge(void)
{
    uart_pt_config_t config = UART_PT_DEFAULT_CONFIG();
    config.port = (uart_port_t)CONFIG_BRIDGE_UART_PORT;
    config.tx_pin = CONFIG_BRIDGE_UART_TX_GPIO;
    config.rx_pin = CONFIG_BRIDGE_UART_RX_GPIO;
    config.baud_rate = CONFIG_BRIDGE_UART_BAUD_RATE;
    config.block_count = CONFIG_BRIDGE_UART_BLOCK_COUNT;
    config.retry_limit = CONFIG_BRIDGE_UART_RETRY_LIMIT;

    esp_err_t err = uart_pt_init(&config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_pt_bind_endpoint(wifi_upload_uart_endpoint, NULL);
    if (err != ESP_OK) {
        return err;
    }
    return uart_pt_start();
}

static esp_err_t start_wifi_bridge(void)
{
    static const wifi_module_config_t config = {
        .ssid = CONFIG_BRIDGE_WIFI_SSID,
        .password = CONFIG_BRIDGE_WIFI_PASSWORD,
        .tcp_host = CONFIG_BRIDGE_TCP_HOST,
        .tcp_port = CONFIG_BRIDGE_TCP_PORT,
        .udp_listen_port = CONFIG_BRIDGE_UDP_LISTEN_PORT,
        .wifi_max_retries = CONFIG_BRIDGE_WIFI_MAX_RETRIES,
        .tcp_max_retries = CONFIG_BRIDGE_TCP_MAX_RETRIES,
        .tcp_retry_delay_ms = CONFIG_BRIDGE_TCP_RETRY_DELAY_MS,
    };
    static const wifi_module_callbacks_t callbacks = {
        .event_cb = bridge_network_event,
        .protocol_cb = bridge_wifi_to_uart,
    };

    return wifi_module_init(&config, &callbacks);
}

void app_main(void)
{
    log_bridge_configuration();

    esp_err_t err = start_uart_bridge();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART bridge start failed: %s", esp_err_to_name(err));
        return;
    }

    err = start_wifi_bridge();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi bridge start failed: %s", esp_err_to_name(err));
        (void)uart_pt_stop();
        return;
    }

    ESP_LOGI(TAG,
             "Bridge started: UART%d TX=%d RX=%d baud=%d -> %s:%d",
             CONFIG_BRIDGE_UART_PORT,
             CONFIG_BRIDGE_UART_TX_GPIO,
             CONFIG_BRIDGE_UART_RX_GPIO,
             CONFIG_BRIDGE_UART_BAUD_RATE,
             CONFIG_BRIDGE_TCP_HOST,
             CONFIG_BRIDGE_TCP_PORT);
}
