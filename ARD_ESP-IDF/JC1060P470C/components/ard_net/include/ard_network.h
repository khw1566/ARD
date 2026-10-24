#pragma once
// Wi-Fi AP+STA, mDNS and the shared /network settings page.
// ESP-IDF port of ARD_software/shared/ArdNetwork. Settings live in the same
// NVS blob ("ard-network"/"settings") the Arduino firmware used, so a board
// re-flashed with this firmware keeps its Wi-Fi and PC server.
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "network_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// role: hostname prefix, e.g. "hmi" -> ard-hmi-xxxxxx.
// legacy_namespace: older Arduino Preferences namespace to import once (may be NULL).
// default_server: PC host used until one is saved on the settings page.
esp_err_t ard_network_start(const char *role, const char *legacy_namespace, const char *default_server);
// Registers GET /, GET /network and POST /network/save.
esp_err_t ard_network_register_http(httpd_handle_t server);

bool ard_network_connected(void);
// Resolved PC IPv4 (or the configured host until it resolves).
void ard_network_server(char *out, size_t size);
void ard_network_local_ip(char *out, size_t size);
// Station RSSI in dBm, or -127 while disconnected.
int ard_network_rssi(void);
void ard_network_print_setup(void);

// Copies a coherent snapshot; passwords must never be logged or displayed.
void ard_network_get_settings(ard_network_settings_t *out);
// These blocking functions belong on a worker, never in an LVGL callback.
#define ARD_WIFI_SCAN_MAX 20
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
} ard_network_ap_t;
esp_err_t ard_network_scan(ard_network_ap_t *out, uint16_t capacity, uint16_t *count);
// Connects live, waits up to 25 s for an IP, then persists the existing NVS blob.
// Failure restores the previous station configuration. The setup AP stays on.
esp_err_t ard_network_join(const char *ssid, const char *password);
// Web portal: store for next boot, serialized against scan/join.
esp_err_t ard_network_store_settings(const ard_network_settings_t *next);
const char *ard_network_hostname(void);
const char *ard_network_password(void);
const char *ard_network_token(void);
void ard_network_restart_later(void);

#ifdef __cplusplus
}
#endif
