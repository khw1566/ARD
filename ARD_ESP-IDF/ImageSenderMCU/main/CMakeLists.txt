idf_component_register(
    SRCS "main.c" "camera.c" "img_tx.c" "pc_link.c" "cmd_server.c"
    INCLUDE_DIRS "."
    REQUIRES ard_net ard_proto esp32-camera esp_http_client esp_http_server json esp_timer esp_psram nvs_flash esp_driver_uart lwip
)
