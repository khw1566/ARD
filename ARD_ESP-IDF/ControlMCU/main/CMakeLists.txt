idf_component_register(
    SRCS "main.c" "inputs.c" "led.c" "audio.c" "pc_link.c" "cmd_server.c"
    INCLUDE_DIRS "."
    REQUIRES ard_net esp_driver_gpio esp_driver_i2s esp_driver_uart esp_driver_tsens esp_http_client
             esp_http_server esp_netif esp_timer json lwip nvs_flash
)
