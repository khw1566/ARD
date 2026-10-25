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
// For boards with live touch setup. The caller must serialize these calls with
// ard_wifi_tick(). begin_manual waits for the old station's disconnect event.
// Manual entry may target a hidden SSID; automatic history still requires scan visibility.
// Call tick while waiting for an IP (retries only the submitted SSID), then end
// the manual session after storing settings or restoring the previous profile.
esp_err_t ard_wifi_begin_manual(const ard_network_settings_t *settings);
void ard_wifi_end_manual(void);
// Serialized with tick/scan/join. Waits up to 2.5 s for disconnect completion.
esp_err_t ard_wifi_disconnect_wait(void);
bool ard_wifi_active_profile(ard_wifi_profile_t *out);
// Returns a saved successful profile for the visible setup password field.
bool ard_wifi_saved_profile(const char *ssid, ard_wifi_profile_t *out);

#ifdef __cplusplus
}
#endif
