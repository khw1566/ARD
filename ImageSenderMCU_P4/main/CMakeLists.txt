set(ARD_NETWORK_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../shared/ArdNetwork/src")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../shared/ArdNetwork/src/network_settings.h")
    set(ARD_NETWORK_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../shared/ArdNetwork/src")
endif()
idf_component_register(
    SRCS "main.c" "cam_ov5647.c" "ard_sender.c" "ard_network.c" "ard_network_http.c"
    INCLUDE_DIRS "." "${ARD_NETWORK_INCLUDE}"
    REQUIRES esp_http_client json esp_timer esp_eth esp_http_server esp_event nvs_flash esp_netif esp_wifi mdns mbedtls driver esp_driver_cam esp_driver_jpeg esp_driver_isp esp_cam_sensor esp_hw_support
)
