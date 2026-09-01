/**
 * @file wifi_module.c
 * @brief 从 DSS station/net_utils_ls.c 解耦的可移植 Wi-Fi、双 TCP 任务与 UDP 唤醒实现。
 */
#include "wifi_module.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define WIFI_READY_BIT   BIT0
#define TCP_REQUEST_BIT  BIT1
#define TCP_READY_BIT    BIT2
#define TX_QUEUE_DEPTH   32
#define STATE_QUEUE_DEPTH 24
#define TCP_RX_STACK     13312
#define TCP_TX_STACK     8192
#define UDP_STACK        4096

typedef struct { size_t length; uint8_t data[]; } tcp_packet_t;
typedef enum {
    ST_INIT, ST_WIFI_STARTED, ST_WIFI_CONNECTED, ST_WIFI_DISCONNECTED, ST_IP,
    ST_TCP_REQUESTED, ST_TCP_STOPPED, ST_TCP_CONNECTED, ST_TCP_DISCONNECTED,
    ST_TCP_RETRY, ST_TCP_EXHAUSTED, ST_UDP_STARTED, ST_UDP_STOPPED, ST_UDP_RESTART
} state_msg_id_t;
typedef struct { state_msg_id_t id; } state_msg_t;

static const char *TAG = "wifi_dss";
static wifi_module_config_t s_config;
static wifi_module_callbacks_t s_callbacks;
static wifi_module_state_t s_state;
static QueueHandle_t s_state_queue;
static QueueHandle_t s_tx_queue;
static EventGroupHandle_t s_bits;
static SemaphoreHandle_t s_state_lock;
static SemaphoreHandle_t s_socket_lock;
static TaskHandle_t s_state_task_handle;
static TaskHandle_t s_tcp_rx_handle;
static TaskHandle_t s_tcp_tx_handle;
static TaskHandle_t s_udp_handle;
static int s_tcp_socket = -1;
static bool s_initialized;

static void tcp_rx_task(void *arg);
static void tcp_tx_task(void *arg);
static void udp_rx_task(void *arg);

static void post_state(state_msg_id_t id)
{
    state_msg_t msg = { .id = id };
    if (s_state_queue != NULL) (void)xQueueSend(s_state_queue, &msg, 0);
}

static void copy_state(wifi_module_state_t *out)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_state_lock);
}

static void emit_event(wifi_module_event_t event)
{
    if (s_callbacks.event_cb != NULL) {
        wifi_module_state_t snapshot;
        copy_state(&snapshot);
        s_callbacks.event_cb(event, &snapshot, s_callbacks.user_ctx);
    }
}

static void close_tcp_socket(void)
{
    xSemaphoreTake(s_socket_lock, portMAX_DELAY);
    if (s_tcp_socket >= 0) {
        shutdown(s_tcp_socket, SHUT_RDWR);
        close(s_tcp_socket);
        s_tcp_socket = -1;
    }
    xSemaphoreGive(s_socket_lock);
    xEventGroupClearBits(s_bits, TCP_READY_BIT);
}

static esp_err_t create_tcp_tasks(void)
{
    xEventGroupSetBits(s_bits, TCP_REQUEST_BIT);
    if (s_tcp_rx_handle == NULL && xTaskCreatePinnedToCore(tcp_rx_task, "tcp_client", TCP_RX_STACK, NULL, 7, &s_tcp_rx_handle, 0) != pdPASS) return ESP_ERR_NO_MEM;
    if (s_tcp_tx_handle == NULL && xTaskCreatePinnedToCore(tcp_tx_task, "tcp_send_response", TCP_TX_STACK, NULL, 3, &s_tcp_tx_handle, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static void start_udp_task(void)
{
    if (s_config.udp_listen_port != 0 && s_udp_handle == NULL) {
        if (xTaskCreate(udp_rx_task, "udp_receiver", UDP_STACK, NULL, 3, &s_udp_handle) != pdPASS) ESP_LOGE(TAG, "UDP task create failed");
    }
}

static void state_task(void *arg)
{
    (void)arg;
    state_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_state_queue, &msg, portMAX_DELAY) != pdPASS) continue;
        wifi_module_event_t notify = WIFI_MODULE_EVENT_WIFI_STARTED;
        bool do_notify = true;
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        switch (msg.id) {
        case ST_INIT: s_state.initialized = true; s_state.wifi_enabled = true; break;
        case ST_WIFI_STARTED: s_state.wifi_enabled = true; notify = WIFI_MODULE_EVENT_WIFI_STARTED; break;
        case ST_WIFI_CONNECTED: s_state.wifi_connected = true; s_state.wifi_retry_count = 0; notify = WIFI_MODULE_EVENT_WIFI_CONNECTED; break;
        case ST_WIFI_DISCONNECTED:
            s_state.wifi_connected = false; s_state.ip_acquired = false; s_state.tcp_connected = false;
            ++s_state.wifi_retry_count; notify = WIFI_MODULE_EVENT_WIFI_DISCONNECTED; break;
        case ST_IP: s_state.ip_acquired = true; s_state.wifi_retry_count = 0; s_state.tcp_requested = true; notify = WIFI_MODULE_EVENT_IP_ACQUIRED; break;
        case ST_TCP_REQUESTED: s_state.tcp_requested = true; do_notify = false; break;
        case ST_TCP_STOPPED: s_state.tcp_requested = false; s_state.tcp_connected = false; do_notify = false; break;
        case ST_TCP_CONNECTED: s_state.tcp_connected = true; s_state.tcp_retry_count = 0; notify = WIFI_MODULE_EVENT_TCP_CONNECTED; break;
        case ST_TCP_DISCONNECTED: s_state.tcp_connected = false; notify = WIFI_MODULE_EVENT_TCP_DISCONNECTED; break;
        case ST_TCP_RETRY: ++s_state.tcp_retry_count; do_notify = false; break;
        case ST_TCP_EXHAUSTED: s_state.tcp_connected = false; s_state.tcp_requested = false; notify = WIFI_MODULE_EVENT_TCP_RETRY_EXHAUSTED; break;
        case ST_UDP_STARTED: s_state.udp_listening = true; notify = WIFI_MODULE_EVENT_UDP_LISTENING; break;
        case ST_UDP_STOPPED: s_state.udp_listening = false; do_notify = false; break;
        case ST_UDP_RESTART: s_state.udp_listening = false; s_state.tcp_requested = true; notify = WIFI_MODULE_EVENT_UDP_TCP_RESTART; break;
        }
        ++s_state.state_revision;
        xSemaphoreGive(s_state_lock);

        if (msg.id == ST_IP || msg.id == ST_TCP_REQUESTED || msg.id == ST_UDP_RESTART) (void)create_tcp_tasks();
        if (msg.id == ST_TCP_EXHAUSTED || msg.id == ST_TCP_STOPPED) start_udp_task();
        if (msg.id == ST_WIFI_DISCONNECTED) {
            wifi_module_state_t snapshot; copy_state(&snapshot);
            if (snapshot.wifi_enabled && (s_config.wifi_max_retries == 0 || snapshot.wifi_retry_count <= s_config.wifi_max_retries)) (void)esp_wifi_connect();
        }
        if (do_notify) emit_event(notify);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { post_state(ST_WIFI_STARTED); (void)esp_wifi_connect(); }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) post_state(ST_WIFI_CONNECTED);
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_bits, WIFI_READY_BIT);
        close_tcp_socket();
        post_state(ST_WIFI_DISCONNECTED);
        post_state(ST_TCP_DISCONNECTED);
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_bits, WIFI_READY_BIT);
        post_state(ST_IP);
    }
}

esp_err_t wifi_module_parse_at_frame(const uint8_t *frame, size_t length, wifi_module_at_command_t *command)
{
    if (frame == NULL || command == NULL || length != 19 || memcmp(frame, "AT+\0", 4) || memcmp(frame + 16, "EAT", 3)) return ESP_ERR_INVALID_ARG;
    memcpy(command->command, frame + 4, 4); command->parameter = 0;
    for (size_t i = 0; i < 8; ++i) command->parameter |= (uint64_t)frame[8 + i] << (i * 8);
    return ESP_OK;
}

static void protocol_placeholder(wifi_module_protocol_t type, const uint8_t *data, size_t length)
{
    if (s_callbacks.protocol_cb != NULL) s_callbacks.protocol_cb(type, data, length, s_callbacks.user_ctx);
    else ESP_LOGI(TAG, "protocol %d placeholder, length=%u", type, (unsigned)length);
}

static void dispatch_packet(const uint8_t *data, size_t length)
{
    if (length >= 3 && memcmp(data, "HRT", 3) == 0) { static const uint8_t response[] = "ALE"; (void)wifi_module_add_to_tcp_queue(response, 3, 0); return; }
    if ((length >= 3 && memcmp(data, "ACK", 3) == 0) || (length >= 4 && memcmp(data + 1, "ACK", 3) == 0)) { protocol_placeholder(WIFI_MODULE_PROTOCOL_ACK, data, length); return; }
    if (length >= 19 && memcmp(data, "AT+\0", 4) == 0 && memcmp(data + 16, "EAT", 3) == 0) {
        wifi_module_at_command_t cmd;
        protocol_placeholder(WIFI_MODULE_PROTOCOL_AT, data, 19);
        if (wifi_module_parse_at_frame(data, 19, &cmd) == ESP_OK && s_callbacks.at_command_cb != NULL) {
            s_callbacks.at_command_cb(&cmd, s_callbacks.user_ctx);
        }
        return;
    }
    if (length >= 31 && memcmp(data, "TIME", 4) == 0 && memcmp(data + 28, "EOF", 3) == 0) { protocol_placeholder(WIFI_MODULE_PROTOCOL_TIME, data, length); return; }
    if (length >= 4 && memcmp(data, "CFG\0", 4) == 0) { protocol_placeholder(WIFI_MODULE_PROTOCOL_CFG, data, length); return; }
    if (length >= 39 && memcmp(data, "UUID", 4) == 0 && memcmp(data + 36, "EOU", 3) == 0) { protocol_placeholder(WIFI_MODULE_PROTOCOL_UUID, data, length); return; }
    if (length >= 11 && memcmp(data, "OTA\0", 4) == 0 && memcmp(data + length - 3, "EOO", 3) == 0) { protocol_placeholder(WIFI_MODULE_PROTOCOL_OTA, data, length); return; }
    protocol_placeholder(WIFI_MODULE_PROTOCOL_RAW, data, length);
}

static int connect_server(void)
{
    struct sockaddr_in peer = { .sin_family = AF_INET, .sin_port = htons(s_config.tcp_port) };
    if (inet_pton(AF_INET, s_config.tcp_host, &peer.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return -1;
    struct timeval timeout = { .tv_sec = 0, .tv_usec = 500000 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&peer, sizeof(peer)) != 0) { close(fd); return -1; }
    xSemaphoreTake(s_socket_lock, portMAX_DELAY); s_tcp_socket = fd; xSemaphoreGive(s_socket_lock);
    xEventGroupSetBits(s_bits, TCP_READY_BIT); return fd;
}

static void tcp_rx_task(void *arg)
{
    (void)arg; uint8_t buffer[WIFI_MODULE_MAX_RX_SIZE]; uint8_t retries = 0;
    while ((xEventGroupGetBits(s_bits) & TCP_REQUEST_BIT) != 0) {
        EventBits_t ready = xEventGroupWaitBits(s_bits, WIFI_READY_BIT | TCP_REQUEST_BIT,
                                                pdFALSE, pdTRUE, pdMS_TO_TICKS(200));
        if ((ready & TCP_REQUEST_BIT) == 0) break;
        if ((ready & WIFI_READY_BIT) == 0) continue;
        int fd = connect_server();
        if (fd < 0) {
            post_state(ST_TCP_RETRY); ++retries;
            if (s_config.tcp_max_retries != 0 && retries >= s_config.tcp_max_retries) { xEventGroupClearBits(s_bits, TCP_REQUEST_BIT); post_state(ST_TCP_EXHAUSTED); break; }
            vTaskDelay(pdMS_TO_TICKS(s_config.tcp_retry_delay_ms ? s_config.tcp_retry_delay_ms : 1000)); continue;
        }
        retries = 0; post_state(ST_TCP_CONNECTED);
        while ((xEventGroupGetBits(s_bits) & TCP_REQUEST_BIT) != 0) {
            int length = recv(fd, buffer, sizeof(buffer), 0);
            if (length > 0) dispatch_packet(buffer, (size_t)length);
            else if (length == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) break;
        }
        close_tcp_socket(); post_state(ST_TCP_DISCONNECTED);
    }
    s_tcp_rx_handle = NULL; vTaskDelete(NULL);
}

static void tcp_tx_task(void *arg)
{
    (void)arg; tcp_packet_t *packet = NULL;
    while ((xEventGroupGetBits(s_bits) & TCP_REQUEST_BIT) != 0) {
        if ((xEventGroupGetBits(s_bits) & TCP_READY_BIT) != 0 && xQueueReceive(s_tx_queue, &packet, 0) == pdPASS) {
            size_t offset = 0; bool failed = false;
            while (offset < packet->length) {
                xSemaphoreTake(s_socket_lock, portMAX_DELAY); int fd = s_tcp_socket;
                int sent = fd >= 0 ? send(fd, packet->data + offset, packet->length - offset, 0) : -1;
                xSemaphoreGive(s_socket_lock);
                if (sent <= 0) { failed = true; break; } offset += (size_t)sent;
            }
            if (failed) { (void)xQueueSendToFront(s_tx_queue, &packet, 0); close_tcp_socket(); }
            else free(packet);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_tcp_tx_handle = NULL; vTaskDelete(NULL);
}

static void udp_rx_task(void *arg)
{
    (void)arg; uint8_t buffer[512]; int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = htons(s_config.udp_listen_port), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (fd < 0 || bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) { if (fd >= 0) close(fd); s_udp_handle = NULL; vTaskDelete(NULL); }
    post_state(ST_UDP_STARTED);
    for (;;) {
        int length = recvfrom(fd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (length == 10 && memcmp(buffer, "TCPCONNECT", 10) == 0) { post_state(ST_UDP_RESTART); break; }
        if (length > 0) protocol_placeholder(WIFI_MODULE_PROTOCOL_RAW, buffer, (size_t)length);
    }
    close(fd); post_state(ST_UDP_STOPPED); s_udp_handle = NULL; vTaskDelete(NULL);
}

esp_err_t wifi_module_init(const wifi_module_config_t *config, const wifi_module_callbacks_t *callbacks)
{
    if (config == NULL || config->ssid[0] == 0 || config->tcp_host[0] == 0 || config->tcp_port == 0 || s_initialized) return ESP_ERR_INVALID_ARG;
    s_config = *config; if (callbacks != NULL) s_callbacks = *callbacks;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif"); ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_state_queue = xQueueCreate(STATE_QUEUE_DEPTH, sizeof(state_msg_t)); s_tx_queue = xQueueCreate(TX_QUEUE_DEPTH, sizeof(tcp_packet_t *));
    s_bits = xEventGroupCreate(); s_state_lock = xSemaphoreCreateMutex(); s_socket_lock = xSemaphoreCreateMutex();
    if (!s_state_queue || !s_tx_queue || !s_bits || !s_state_lock || !s_socket_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(state_task, "wifi_state", 4096, NULL, 8, &s_state_task_handle) != pdPASS) return ESP_ERR_NO_MEM;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT(); ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG, "ip events");
    wifi_config_t station = {0}; strlcpy((char *)station.sta.ssid, s_config.ssid, sizeof(station.sta.ssid)); strlcpy((char *)station.sta.password, s_config.password, sizeof(station.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode"); ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station), TAG, "config");
    s_initialized = true; post_state(ST_INIT); return esp_wifi_start();
}

esp_err_t wifi_module_add_to_tcp_queue(const void *data, size_t length, TickType_t timeout)
{
    if (!s_initialized || data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    tcp_packet_t *packet = malloc(sizeof(*packet) + length); if (packet == NULL) return ESP_ERR_NO_MEM;
    packet->length = length; memcpy(packet->data, data, length);
    if (xQueueSend(s_tx_queue, &packet, timeout) != pdPASS) { free(packet); return ESP_ERR_TIMEOUT; } return ESP_OK;
}
esp_err_t wifi_module_send(const void *data, size_t length, TickType_t timeout) { return wifi_module_add_to_tcp_queue(data, length, timeout); }
esp_err_t wifi_module_tcp_start(void) { if (!s_initialized) return ESP_ERR_INVALID_STATE; post_state(ST_TCP_REQUESTED); return ESP_OK; }
esp_err_t wifi_module_tcp_stop(void) { if (!s_initialized) return ESP_ERR_INVALID_STATE; xEventGroupClearBits(s_bits, TCP_REQUEST_BIT); close_tcp_socket(); post_state(ST_TCP_STOPPED); return ESP_OK; }
esp_err_t wifi_module_tcp_restart(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    close_tcp_socket();
    post_state(ST_TCP_REQUESTED);
    return ESP_OK;
}
esp_err_t wifi_module_get_state(wifi_module_state_t *snapshot) { if (!s_initialized || snapshot == NULL) return ESP_ERR_INVALID_ARG; copy_state(snapshot); return ESP_OK; }
esp_err_t wifi_module_set_wifi_config(const char *ssid, const char *password)
{
    if (!s_initialized || !ssid || !password || !ssid[0]) return ESP_ERR_INVALID_ARG;
    strlcpy(s_config.ssid, ssid, sizeof(s_config.ssid)); strlcpy(s_config.password, password, sizeof(s_config.password));
    wifi_config_t station = {0}; strlcpy((char *)station.sta.ssid, ssid, sizeof(station.sta.ssid)); strlcpy((char *)station.sta.password, password, sizeof(station.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station), TAG, "update config"); (void)esp_wifi_disconnect(); return esp_wifi_connect();
}

