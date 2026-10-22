#include "ard_network.h"
#include "ard_wifi.h"
#include "ard_setup_qr.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

#define RETRY_US   (15 * 1000000LL)
#define RESOLVE_US (30 * 1000000LL)

static const char *TAG = "ARD_NET";
static ard_network_settings_t settings = {.version = 1};
static char hostname[48], password[16], token[33], server_address[65], sta_ip[16];
static bool sta_ready, wifi_ok;
static int64_t restart_at;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

const ard_network_settings_t *ard_network_settings(void) { return &settings; }
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
    wifi_ap_record_t ap;
    return ard_network_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -127;
}

void ard_network_restart_later(void) {
    taskENTER_CRITICAL(&state_lock);
    restart_at = esp_timer_get_time() + 1500000;
    taskEXIT_CRITICAL(&state_lock);
}

void ard_network_print_setup(void) {
    ard_setup_qr_print();
    ESP_LOGI(TAG, "AP: %s | Password: %s | Login: admin", hostname, password);
    ESP_LOGI(TAG, "Setup: http://192.168.4.1/network or http://%s.local/network", hostname);
    ESP_LOGI(TAG, "SSID: %s | PC server: %s", settings.ssid[0] ? settings.ssid : "(not set)", settings.server);
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
    strlcpy(host, settings.server, sizeof(host));
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
        if (changed) ESP_LOGI(TAG, "PC server %s -> %s", settings.server, resolved);
    } else ESP_LOGW(TAG, "PC host lookup failed for %s; retry in 30s", settings.server);
}

static void network_task(void *arg) {
    int64_t last_resolve = 0;
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
        if (wifi_ok) ard_wifi_tick();
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
    load_settings(legacy_namespace, default_server);
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
    wifi_ok = err == ESP_OK;
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
