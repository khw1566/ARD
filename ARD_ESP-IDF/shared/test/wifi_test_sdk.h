#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
const char *esp_err_to_name(esp_err_t err);
typedef const void *esp_event_base_t;
#define WIFI_EVENT ((esp_event_base_t)1)
#define IP_EVENT ((esp_event_base_t)2)
enum { WIFI_EVENT_STA_DISCONNECTED, IP_EVENT_STA_GOT_IP, IP_EVENT_STA_LOST_IP };
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t fn, void *arg);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
int64_t esp_timer_get_time(void);
enum { WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK, WIFI_ALL_CHANNEL_SCAN, WIFI_CONNECT_AP_BY_SIGNAL, WIFI_IF_STA };
typedef struct { uint8_t ssid[33]; int rssi, authmode; } wifi_ap_record_t;
typedef struct { struct { struct { int min, max; } active; } scan_time; } wifi_scan_config_t;
typedef union { struct { uint8_t ssid[32], password[64]; int scan_method, sort_method; } sta; } wifi_config_t;
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_set_config(int iface, wifi_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
typedef int nvs_handle_t;
enum { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
