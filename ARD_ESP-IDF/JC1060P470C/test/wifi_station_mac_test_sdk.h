#ifndef WIFI_STATION_MAC_TEST_SDK_H
#define WIFI_STATION_MAC_TEST_SDK_H
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define WIFI_IF_STA 0
#define ESP_LOGI(tag, ...) mac_test_log(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) mac_test_log(tag, __VA_ARGS__)
void mac_test_log(const char *, const char *, ...);
const char *esp_err_to_name(esp_err_t);
typedef struct { uint8_t mac[6]; bool up; } esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
esp_err_t esp_wifi_get_mac(int, uint8_t *);
esp_err_t esp_netif_get_mac(esp_netif_t *, uint8_t *);
esp_err_t esp_netif_set_mac(esp_netif_t *, uint8_t *);
bool esp_netif_is_netif_up(esp_netif_t *);
#endif
