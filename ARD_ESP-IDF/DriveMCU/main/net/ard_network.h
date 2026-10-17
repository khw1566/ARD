#pragma once
// ESP-IDF port of shared/ArdNetwork for the native ESP32 Wi-Fi: SoftAP + STA,
// setup portal (/network), mDNS, PC server address resolution and SNTP.
// Same NVS layout ("ard-network"/"settings") and portal as the other boards.
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "network_settings.h"

esp_err_t ard_network_start(const char *role, const char *legacyNamespace, const char *defaultServer);
bool ard_network_connected(void);
void ard_network_server(char *out, size_t size); // resolved PC IPv4 (or the configured host)
void ard_network_ip(char *out, size_t size);
int ard_network_rssi(void);
void ard_network_print_setup(void);

// Used by the portal handlers.
const ard_network_settings_t *ard_network_settings(void);
const char *ard_network_hostname(void);
const char *ard_network_password(void);
const char *ard_network_token(void);
void ard_network_restart_later(void);
esp_err_t ard_network_register_http(httpd_handle_t server);
