#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "network_settings.h"

void ard_network_start(void);
void ard_network_eth(bool ready, const char *ip);
void ard_network_server(char *out, size_t size);
esp_err_t ard_network_register_http(httpd_handle_t server);
const ard_network_settings_t *ard_network_settings(void);
const char *ard_network_hostname(void);
const char *ard_network_password(void);
const char *ard_network_token(void);
bool ard_network_connected(void);
void ard_network_restart_later(void);
