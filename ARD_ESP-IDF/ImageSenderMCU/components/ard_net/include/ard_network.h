#pragma once
// Wi-Fi AP+STA, mDNS and the shared /network settings page (ESP-IDF port of
// ARD_software/shared/ArdNetwork). Settings live in NVS "ard-network/settings",
// the same blob the Arduino firmware used, so a re-flashed board keeps its Wi-Fi.
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "network_settings.h"

// role: hostname prefix, e.g. "image" -> ard-image-xxxxxx.
// default_server: PC host used until one is saved on the settings page.
void ard_network_start(const char *role, const char *default_server);
esp_err_t ard_network_register_http(httpd_handle_t server);

bool ard_network_connected(void);
// Resolved PC IPv4 (or the configured host until it resolves).
void ard_network_server(char *out, size_t size);
void ard_network_local_ip(char *out, size_t size);
int ard_network_rssi(void);
void ard_network_print_setup(void);

const ard_network_settings_t *ard_network_settings(void);
const char *ard_network_hostname(void);
const char *ard_network_password(void);
const char *ard_network_token(void);
void ard_network_restart_later(void);
