idf_component_register(
    SRCS "main.cpp" "store.cpp" "dns_proxy.cpp" "web_ui.cpp" "display.cpp" "speedtest.cpp"
    INCLUDE_DIRS "." "../include"
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server esp_timer json lwip esp_lcd esp_driver_gpio esp_driver_spi esp_driver_ledc esp_http_client mbedtls
)
