idf_component_register(
    SRCS "../../shared/ard_wifi.c" "../../shared/ard_setup_qr.c" "../../shared/third_party/qrcodegen.c"
         "app_main.cpp" "stepper_engine.cpp" "motion.cpp" "commands.cpp" "command_servers.cpp"
         "telemetry.cpp" "sensors.cpp" "display.cpp" "ard_wire.cpp" "bmp280.cpp" "ssd1306.cpp"
         "gps.cpp" "buzzer.cpp" "net/ard_network.cpp" "net/ard_network_http.cpp"
         "third_party/vl53l0x/VL53L0X.cpp"
    INCLUDE_DIRS "." "net" "../../shared" "third_party/vl53l0x"
    REQUIRES esp_driver_gptimer esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc
             esp_timer esp_wifi esp_netif esp_event esp_http_client esp_http_server nvs_flash
             json mbedtls lwip esp_system
)
