idf_component_register(
    SRCS "uart_pt.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_gpio esp_driver_uart
    PRIV_REQUIRES freertos
)

