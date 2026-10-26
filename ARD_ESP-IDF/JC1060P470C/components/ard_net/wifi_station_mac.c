#include "wifi_station_mac.h"
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "WIFI_MAC";

static bool valid_station_mac(const uint8_t mac[6]) {
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= mac[i];
    return any != 0 && (mac[0] & 1) == 0;
}

esp_err_t ard_wifi_sync_station_mac(void) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Cannot synchronize STA MAC: station interface is missing");
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t radio[6] = {0}, cached[6] = {0}, verified[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, radio);
    ESP_LOGI(TAG, "Radio STA MAC %02x:%02x:%02x:%02x:%02x:%02x result=%s",
             radio[0], radio[1], radio[2], radio[3], radio[4], radio[5], esp_err_to_name(err));
    if (err != ESP_OK) return err;
    if (!valid_station_mac(radio)) {
        ESP_LOGE(TAG, "Cannot synchronize STA MAC: radio returned an invalid unicast address");
        return ESP_ERR_INVALID_ARG;
    }
    err = esp_netif_get_mac(netif, cached);
    ESP_LOGI(TAG, "Host STA MAC %02x:%02x:%02x:%02x:%02x:%02x up=%d result=%s",
             cached[0], cached[1], cached[2], cached[3], cached[4], cached[5],
             esp_netif_is_netif_up(netif), esp_err_to_name(err));
    if (err != ESP_OK) return err;
    if (!memcmp(radio, cached, sizeof(radio))) return ESP_OK;
    if (esp_netif_is_netif_up(netif)) {
        ESP_LOGE(TAG, "STA MAC differs while the interface is up; refusing a live address change");
        return ESP_ERR_INVALID_STATE;
    }
    err = esp_netif_set_mac(netif, radio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Host STA MAC synchronization failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_netif_get_mac(netif, verified);
    ESP_LOGI(TAG, "Updated host STA MAC %02x:%02x:%02x:%02x:%02x:%02x result=%s",
             verified[0], verified[1], verified[2], verified[3], verified[4], verified[5],
             esp_err_to_name(err));
    if (err != ESP_OK) return err;
    if (memcmp(verified, radio, sizeof(radio))) {
        ESP_LOGE(TAG, "Host STA MAC readback does not match the radio address");
        return ESP_FAIL;
    }
    return ESP_OK;
}
