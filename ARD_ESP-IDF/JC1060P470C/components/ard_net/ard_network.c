#include "ard_network.h"
#include "ard_wifi.h"
#include "ard_setup_qr.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "mdns.h"
#include "nvs.h"

#define RESOLVE_US (30 * 1000000LL)

static const char *TAG = "ARD_NET";
static ard_network_settings_t settings = {.version = 1};
static char hostname[48], password[16], token[33], server_address[65], sta_ip[16];
static bool sta_ready, wifi_ok;
static int sta_rssi = -127;
static bool settings_loaded;
static SemaphoreHandle_t wifi_ops;
static int64_t restart_at;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

void ard_network_get_settings(ard_network_settings_t *out) {
    if (!out) return;
    taskENTER_CRITICAL(&state_lock);
    if (settings_loaded) *out = settings;
    else memset(out, 0, sizeof(*out));
    taskEXIT_CRITICAL(&state_lock);
}
const char *ard_network_hostname(void) { return hostname; }
const char *ard_network_password(void) { return password; }
const char *ard_network_token(void) { return token; }

bool ard_network_connected(void) {
    taskENTER_CRITICAL(&state_lock);
    bool ready = sta_ready;
    taskEXIT_CRITICAL(&state_lock);
    return ready;
}

void ard_network_server(char *out, size_t size) {
    taskENTER_CRITICAL(&state_lock);
    strlcpy(out, server_address, size);
    taskEXIT_CRITICAL(&state_lock);
}

void ard_network_local_ip(char *out, size_t size) {
    taskENTER_CRITICAL(&state_lock);
    strlcpy(out, sta_ready ? sta_ip : "0.0.0.0", size);
    taskEXIT_CRITICAL(&state_lock);
}

int ard_network_rssi(void) {
    taskENTER_CRITICAL(&state_lock);
    int rssi = sta_ready ? sta_rssi : -127;
    taskEXIT_CRITICAL(&state_lock);
    return rssi;
}

void ard_network_restart_later(void) {
    taskENTER_CRITICAL(&state_lock);
    restart_at = esp_timer_get_time() + 1500000;
    taskEXIT_CRITICAL(&state_lock);
}

void ard_network_print_setup(void) {
    ard_setup_qr_print();
    ard_network_settings_t snapshot;
    ard_network_get_settings(&snapshot);
    ESP_LOGI(TAG, "AP: %s | Password: %s | Login: admin", hostname, password);
    ESP_LOGI(TAG, "Setup: http://192.168.4.1/network or http://%s.local/network", hostname);
    ESP_LOGI(TAG, "SSID: %s | PC server: %s", snapshot.ssid[0] ? snapshot.ssid : "(not set)", snapshot.server);
}

static esp_err_t store_settings(const ard_network_settings_t *next) {
    if (!ard_network_valid(next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ard-network", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "settings", next, sizeof(*next));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t ard_network_store_settings(const ard_network_settings_t *next) {
    if (!next || !wifi_ops) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(wifi_ops, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = store_settings(next);
    xSemaphoreGive(wifi_ops);
    return err;
}

static esp_err_t begin_wifi_operation(void) {
    taskENTER_CRITICAL(&state_lock);
    bool ready = wifi_ok;
    taskEXIT_CRITICAL(&state_lock);
    if (!ready || !wifi_ops) return ESP_ERR_WIFI_NOT_STARTED;
    return xSemaphoreTake(wifi_ops, pdMS_TO_TICKS(5000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t ard_network_scan(ard_network_ap_t *out, uint16_t capacity, uint16_t *count) {
    if (!out || !count || !capacity || capacity > ARD_WIFI_SCAN_MAX) return ESP_ERR_INVALID_ARG;
    *count = 0;
    esp_err_t err = begin_wifi_operation();
    if (err != ESP_OK) return err;
    wifi_scan_config_t config = {0};
    config.show_hidden = false;
    err = esp_wifi_scan_start(&config, true);
    if (err == ESP_ERR_WIFI_STATE && !ard_network_connected()) {
        // Stop a failing background association, then scan without retry races.
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        err = esp_wifi_scan_start(&config, true);
    }
    if (err == ESP_OK) {
        wifi_ap_record_t records[ARD_WIFI_SCAN_MAX];
        uint16_t found = ARD_WIFI_SCAN_MAX;
        err = esp_wifi_scan_get_ap_records(&found, records);
        if (err == ESP_OK) {
            if (found > ARD_WIFI_SCAN_MAX) found = ARD_WIFI_SCAN_MAX;
            for (uint16_t i = 0; i < found && *count < capacity; ++i) {
                if (!records[i].ssid[0]) continue;
                bool secure = records[i].authmode != WIFI_AUTH_OPEN;
                bool duplicate = false;
                for (uint16_t j = 0; j < *count; ++j)
                    if (!strncmp(out[j].ssid, (char *)records[i].ssid, 32) && out[j].secure == secure) duplicate = true;
                if (duplicate) continue;
                ard_network_ap_t *ap = &out[(*count)++];
                memcpy(ap->ssid, records[i].ssid, 32);
                ap->ssid[32] = 0;
                ap->rssi = records[i].rssi;
                ap->channel = records[i].primary;
                ap->secure = secure;
            }
        }
        if (err != ESP_OK) esp_wifi_clear_ap_list();
    }
    xSemaphoreGive(wifi_ops);
    return err;
}

esp_err_t ard_network_join(const char *ssid, const char *station_password) {
    if (!ssid || !station_password || strnlen(ssid, 33) > 32 || strnlen(station_password, 65) > 64)
        return ESP_ERR_INVALID_ARG;
    ard_network_settings_t previous, next;
    ard_network_get_settings(&previous);
    next = previous;
    strlcpy(next.ssid, ssid, sizeof(next.ssid));
    strlcpy(next.password, station_password, sizeof(next.password));
    if (!ard_network_valid(&next)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = begin_wifi_operation();
    if (err != ESP_OK) return err;
    // History may have connected a different saved SSID than the configured one.
    // Restore that actual profile if this manual attempt fails.
    ard_wifi_profile_t active;
    if (ard_wifi_active_profile(&active)) {
        memcpy(previous.ssid, active.ssid, sizeof(previous.ssid));
        memcpy(previous.password, active.password, sizeof(previous.password));
    }
    esp_wifi_disconnect();
    taskENTER_CRITICAL(&state_lock);
    sta_ready = false;
    taskEXIT_CRITICAL(&state_lock);
    // Let the old association's disconnect event finish before requesting a new IP.
    vTaskDelay(pdMS_TO_TICKS(200));
    // Updating the shared worker's active profile prevents it from saving a
    // successful touch connection under the preceding history entry.
    err = ard_wifi_begin_manual(&next);
    if (err == ESP_OK) {
        const int64_t deadline = esp_timer_get_time() + 25000000LL;
        bool joined = false;
        while (esp_timer_get_time() < deadline) {
            if (ard_network_connected()) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && !strncmp((char *)ap.ssid, next.ssid, 32)) {
                    joined = true;
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        err = joined ? store_settings(&next) : ESP_ERR_TIMEOUT;
    }
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&state_lock);
        settings = next;
        taskEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "Wi-Fi settings saved after successful connection");
    } else {
        esp_wifi_disconnect();
        taskENTER_CRITICAL(&state_lock);
        sta_ready = false;
        taskEXIT_CRITICAL(&state_lock);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_err_t restore = ard_wifi_begin_manual(&previous);
        if (restore != ESP_OK) ESP_LOGW(TAG, "Previous Wi-Fi profile restore failed: %s", esp_err_to_name(restore));
        ESP_LOGW(TAG, "Wi-Fi setup failed: %s; previous station settings restored", esp_err_to_name(err));
    }
    xSemaphoreGive(wifi_ops);
    return err;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        taskENTER_CRITICAL(&state_lock);
        snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        sta_ready = true;
        taskEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "Wi-Fi connected: %s", sta_ip);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        bool was_ready = ard_network_connected();
        taskENTER_CRITICAL(&state_lock);
        sta_ready = false;
        taskEXIT_CRITICAL(&state_lock);
        if (was_ready) ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d); setup AP stays available", event->reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        taskENTER_CRITICAL(&state_lock);
        sta_ready = false;
        taskEXIT_CRITICAL(&state_lock);
        ESP_LOGW(TAG, "Wi-Fi lost IP");
    }
}

static esp_err_t start_wifi(void) {
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!sta || !ap) return ESP_ERR_NO_MEM;
    // Hostname must be set before either interface starts (DHCP option 12).
    esp_netif_set_hostname(sta, hostname);
    esp_netif_set_hostname(ap, hostname);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    // Credentials come from our own NVS blob; keep the driver from writing a second copy.
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    wifi_config_t ap_cfg = {0}, sta_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, hostname, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(hostname);
    strlcpy((char *)ap_cfg.ap.password, password, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    // The driver arrays accept a full-length SSID/PSK without a terminating NUL.
    memcpy(sta_cfg.sta.ssid, settings.ssid, strlen(settings.ssid));
    memcpy(sta_cfg.sta.password, settings.password, strlen(settings.password));
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if ((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    // Same as WiFi.setSleep(false) in the Arduino build: lower touch-to-command latency.
    esp_wifi_set_ps(WIFI_PS_NONE);
    return ESP_OK;
}

static void resolve_server(void) {
    char host[65], resolved[65] = {0};
    ard_network_settings_t snapshot;
    ard_network_get_settings(&snapshot);
    strlcpy(host, snapshot.server, sizeof(host));
    size_t len = strlen(host);
    if (len > 6 && !strcasecmp(host + len - 6, ".local")) {
        esp_ip4_addr_t addr;
        host[len - 6] = 0;
        if (mdns_query_a(host, 500, &addr) == ESP_OK) snprintf(resolved, sizeof(resolved), IPSTR, IP2STR(&addr));
    } else {
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *result = NULL;
        if (!getaddrinfo(host, NULL, &hints, &result) && result) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)result->ai_addr)->sin_addr, resolved, sizeof(resolved));
            freeaddrinfo(result);
        }
    }
    if (resolved[0]) {
        taskENTER_CRITICAL(&state_lock);
        bool changed = strcmp(server_address, resolved) != 0;
        strlcpy(server_address, resolved, sizeof(server_address));
        taskEXIT_CRITICAL(&state_lock);
        if (changed) ESP_LOGI(TAG, "PC server %s -> %s", snapshot.server, resolved);
    } else ESP_LOGW(TAG, "PC host lookup failed for %s; retry in 30s", snapshot.server);
}

static void network_task(void *arg) {
    int64_t last_resolve = 0, last_rssi = 0;
    bool was_ready = false;
    while (true) {
        int64_t now = esp_timer_get_time();
        taskENTER_CRITICAL(&state_lock);
        int64_t restart = restart_at;
        taskEXIT_CRITICAL(&state_lock);
        if (restart && now >= restart) esp_restart();
        bool ready = ard_network_connected();
        if (ready && !was_ready) last_resolve = now - RESOLVE_US; // resolve right after connecting
        was_ready = ready;
        // One gate serializes shared history/reconnect, web scans and touch setup.
        if (wifi_ok && xSemaphoreTake(wifi_ops, 0) == pdTRUE) {
            ard_wifi_tick();
            if (ard_network_connected() && now - last_rssi >= 2000000LL) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                    taskENTER_CRITICAL(&state_lock);
                    sta_rssi = ap.rssi;
                    taskEXIT_CRITICAL(&state_lock);
                }
                last_rssi = now;
            }
            xSemaphoreGive(wifi_ops);
        }
        if (ready && now - last_resolve >= RESOLVE_US) {
            last_resolve = now;
            resolve_server();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Arduino Preferences stored these as NVS strings in the board's old namespace.
static bool load_legacy(const char *ns, const char *default_server) {
    nvs_handle_t nvs;
    if (!ns || nvs_open(ns, NVS_READONLY, &nvs) != ESP_OK) return false;
    ard_network_settings_t legacy = {.version = 1};
    size_t size = sizeof(legacy.ssid);
    bool ok = nvs_get_str(nvs, "ssid", legacy.ssid, &size) == ESP_OK;
    size = sizeof(legacy.password);
    if (ok && nvs_get_str(nvs, "password", legacy.password, &size) != ESP_OK) legacy.password[0] = 0;
    size = sizeof(legacy.server);
    if (ok && nvs_get_str(nvs, "server_ip", legacy.server, &size) != ESP_OK)
        strlcpy(legacy.server, default_server, sizeof(legacy.server));
    nvs_close(nvs);
    if (!ok || !ard_network_valid(&legacy)) return false;
    settings = legacy;
    ESP_LOGI(TAG, "Imported network settings from legacy namespace '%s'", ns);
    return true;
}

static void load_settings(const char *legacy_ns, const char *default_server) {
    strlcpy(settings.server, default_server, sizeof(settings.server));
    nvs_handle_t nvs;
    if (nvs_open("ard-network", NVS_READONLY, &nvs) == ESP_OK) {
        ard_network_settings_t stored = {0};
        size_t size = sizeof(stored);
        bool ok = nvs_get_blob(nvs, "settings", &stored, &size) == ESP_OK && size == sizeof(stored) &&
                  ard_network_valid(&stored);
        nvs_close(nvs);
        if (ok) {
            settings = stored;
            ESP_LOGI(TAG, "Loaded network settings from NVS");
            return;
        }
    }
    load_legacy(legacy_ns, default_server);
}

esp_err_t ard_network_start(const char *role, const char *legacy_namespace, const char *default_server) {
    wifi_ops = xSemaphoreCreateMutex();
    if (!wifi_ops) return ESP_ERR_NO_MEM;
    load_settings(legacy_namespace, default_server);
    taskENTER_CRITICAL(&state_lock);
    settings_loaded = true;
    taskEXIT_CRITICAL(&state_lock);
    strlcpy(server_address, settings.server, sizeof(server_address));
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) return err;
    // Same naming as the Arduino ArdNetwork library (last three MAC bytes).
    snprintf(hostname, sizeof(hostname), "ard-%s-%02x%02x%02x", role, mac[3], mac[4], mac[5]);
    snprintf(password, sizeof(password), "ARD-%02x%02x%02x!", mac[3], mac[4], mac[5]);
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) != ESP_OK) return err;
    err = start_wifi();
    taskENTER_CRITICAL(&state_lock);
    wifi_ok = err == ESP_OK;
    taskEXIT_CRITICAL(&state_lock);
    if (wifi_ok) {
        esp_err_t profile_err = ard_wifi_init(&settings);
        if (profile_err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi history init failed: %s", esp_err_to_name(profile_err));
    }
    if (!wifi_ok) ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(hostname);
        mdns_instance_name_set(hostname);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        mdns_service_add(NULL, "_ard", "_tcp", 80, NULL, 0);
        mdns_txt_item_t role_txt[] = {{"role", role}};
        mdns_service_txt_set("_ard", "_tcp", role_txt, 1);
    } else ESP_LOGE(TAG, "mDNS init failed; use the AP IP");
    ard_setup_qr_init(hostname, password);
    ard_network_print_setup();
    if (xTaskCreate(network_task, "ard_network", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return err;
}
