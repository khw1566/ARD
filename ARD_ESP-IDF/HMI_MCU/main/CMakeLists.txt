idf_component_register(
    SRCS "main.c" "lcd.c" "touch.c" "gfx.c" "jpeg.c" "hmi_model.c" "hmi_ui.c" "hmi_setup.c" "hmi_net.c"
    INCLUDE_DIRS "."
    REQUIRES ard_net
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_lcd esp_http_client esp_http_server
                  esp_netif esp_event esp_timer esp_rom json nvs_flash lwip
    EMBED_FILES "assets/logo.rgb565"
)
# hmi_ui.c is shared with the PC simulator, so it takes this option as a plain define.
if(CONFIG_HMI_Z_STEP_BUTTONS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HMI_Z_STEP_BUTTONS=1)
endif()
if(CONFIG_HMI_RAIL_PID)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HMI_RAIL_PID=1)
endif()
