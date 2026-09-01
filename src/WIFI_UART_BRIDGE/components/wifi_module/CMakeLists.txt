idf_component_register(
    SRCS "wifi_module.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common freertos
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash lwip
)

