idf_component_register(
    SRCS "app_main.cpp" "bsp_display.cpp" "panel_init.c" "ui.cpp" "pc_link.cpp"
         "gamepad.cpp" "hid_gamepad_parser.cpp" "controls.cpp"
         "stepper_engine.cpp" "motion.cpp" "commands.cpp" "command_servers.cpp" "telemetry.cpp"
         "sensors.cpp" "ard_wire.cpp" "bmp280.cpp" "gps.cpp" "buzzer.cpp"
         "third_party/vl53l0x/VL53L0X.cpp"
    INCLUDE_DIRS "." "third_party/vl53l0x"
    REQUIRES ard_net esp_driver_gptimer esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc
             esp_driver_jpeg esp_lcd esp_timer esp_wifi esp_netif esp_event esp_http_client esp_http_server
             nvs_flash json lwip esp_system esp_hw_support esp_hid bt
)
