idf_component_register(
    SRCS "ard_network.c" "ard_network_http.c"
         "../../../shared/ard_wifi.c" "../../../shared/ard_setup_qr.c" "../../../shared/third_party/qrcodegen.c"
    INCLUDE_DIRS "include" "../../../shared"
    REQUIRES esp_wifi esp_netif esp_event esp_http_server esp_timer nvs_flash
    PRIV_REQUIRES mbedtls mdns
)
