# ESP-Hosted 2.12.2 C6 slave image, pushed to the C6 when it runs an older release (c6_update.cpp).
set(c6_image "${CMAKE_CURRENT_LIST_DIR}/../c6_firmware/network_adapter.bin")
set(c6_embed "")
if(EXISTS "${c6_image}")
    set(c6_embed "${c6_image}")
endif()

idf_component_register(
    SRCS "onboard_audio.cpp" "ui_settings.cpp" "app_main.cpp" "bsp_display.cpp" "panel_init.c" "ui.cpp" "ui_main.cpp" "pc_link.cpp" "c6_update.cpp" "ui_text.cpp" "ard_font_hangul.c"
         "commands.cpp" "command_servers.cpp" "clock_sync.cpp"
    INCLUDE_DIRS "." "third_party/vl53l0x"
    REQUIRES ard_net esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc
             esp_driver_jpeg esp_lcd esp_timer esp_wifi esp_netif esp_event esp_http_client esp_http_server
             nvs_flash json lwip esp_system esp_hw_support
    PRIV_REQUIRES esp_mm esp_driver_i2s esp_app_format bootloader_support
    EMBED_FILES ${c6_embed}
)
if(c6_embed)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ARD_C6_FW_EMBEDDED=1)
endif()
# Korean UI: route label/placeholder text through the translation table (ui_text.cpp).
# --undefined keeps the wrappers linked even though nothing references them by name.
foreach(fn lv_label_set_text lv_label_set_text_fmt lv_textarea_set_placeholder_text)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}" "-Wl,--undefined=__wrap_${fn}")
endforeach()
