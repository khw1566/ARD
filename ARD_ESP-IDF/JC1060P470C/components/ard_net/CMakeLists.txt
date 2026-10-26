idf_component_register(
    SRCS "ard_network.c" "ard_network_http.c" "wifi_debug.c" "wifi_station_mac.c"
         "../../../shared/ard_wifi.c" "../../../shared/ard_setup_qr.c" "../../../shared/third_party/qrcodegen.c"
    INCLUDE_DIRS "include" "../../../shared"
    REQUIRES esp_wifi esp_netif esp_event esp_http_server esp_timer nvs_flash
    PRIV_REQUIRES mbedtls mdns esp_app_format
)
if(CONFIG_ARD_WIFI_DIAGNOSTICS)
    # Observe the public netif boundary without replacing the Wi-Fi RX callback.
    foreach(fn esp_netif_set_driver_config esp_netif_receive)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}" "-Wl,--undefined=__wrap_${fn}")
    endforeach()
endif()
