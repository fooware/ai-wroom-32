idf_component_register(
    SRCS "app_main.c" "credential_server.c" "displays.c" "network.c" "usage_poll.c"
    INCLUDE_DIRS "."
    REQUIRES ui lvgl esp_lvgl_port esp_lcd esp_lcd_gc9a01 driver
             network_provisioning protocomm esp_wifi esp_event esp_netif nvs_flash
             esp_timer esp_http_server esp_http_client esp-tls mbedtls espressif__cjson
)
