idf_component_register(
    SRCS
    "main.c"
    "udps_handler.c"
    "wifi_handler.c"
    "web_handler.c"
    PRIV_REQUIRES spi_flash nvs_flash esp_wifi esp_http_server esp_timer esp32_udps
    INCLUDE_DIRS "")
