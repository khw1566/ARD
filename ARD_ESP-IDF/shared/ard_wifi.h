#pragma once
#include "wifi_profiles.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARD_WIFI_SCAN_LIMIT 64
typedef struct { char ssid[33], password[65]; int rssi; bool open, saved; } ard_wifi_ap_t;
// Initialize once after esp_wifi_start. All connect/scan calls belong to tick.
esp_err_t ard_wifi_init(const ard_network_settings_t *configured);
void ard_wifi_tick(void);
void ard_wifi_request_scan(void);
size_t ard_wifi_scan_snapshot(ard_wifi_ap_t *out, size_t capacity, bool *busy, esp_err_t *error);

#ifdef __cplusplus
}
#endif
