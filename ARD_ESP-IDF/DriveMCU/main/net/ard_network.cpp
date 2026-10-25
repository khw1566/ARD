#include "ard_network.h"
#include "ard_wifi.h"
#include "ard_setup_qr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "mdns.h"
#include "nvs.h"

static const char *TAG = "ARD_NET";

namespace {

ard_network_settings_t settings = {};
char hostname[48], password[16], token[33];
char serverAddress[65], staIp[16];
bool staReady;
bool sntpStarted;
int64_t restartAt;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
httpd_handle_t http;

bool loadSettings(const char *legacyNamespace) {
    nvs_handle_t nvs;
    if (nvs_open("ard-network", NVS_READONLY, &nvs) == ESP_OK) {
        ard_network_settings_t stored = {};
        size_t size = sizeof(stored);
        const bool ok = nvs_get_blob(nvs, "settings", &stored, &size) == ESP_OK &&
                        size == sizeof(stored) && ard_network_valid(&stored);
        nvs_close(nvs);
        if (ok) { settings = stored; return true; }
    }
    // One-time import of the Arduino firmware's Preferences strings. No hard-coded fallback.
    if (legacyNamespace && nvs_open(legacyNamespace, NVS_READONLY, &nvs) == ESP_OK) {
        ard_network_settings_t legacy = {};
        legacy.version = 1;
        size_t n = sizeof(legacy.ssid);
        bool ok = nvs_get_str(nvs, "ssid", legacy.ssid, &n) == ESP_OK;
        n = sizeof(legacy.password);
        if (ok && nvs_get_str(nvs, "password", legacy.password, &n) != ESP_OK) legacy.password[0] = 0;
        n = sizeof(legacy.server);
        if (nvs_get_str(nvs, "server_ip", legacy.server, &n) != ESP_OK) strlcpy(legacy.server, settings.server, sizeof(legacy.server));
        nvs_close(nvs);
        if (ok && ard_network_valid(&legacy)) {
            settings = legacy;
            ESP_LOGI(TAG, "Imported Wi-Fi settings from legacy namespace '%s'", legacyNamespace);
            return true;
        }
    }
    return false;
}

void startSntp() {
    if (sntpStarted) return;
    sntpStarted = true;
    setenv("TZ", "KST-9", 1); // UTC+9, same as configTime(32400, 0, ...)
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com"));
    config.start = true;
    config.wait_for_sync = false;
    if (esp_netif_sntp_init(&config) != ESP_OK) { sntpStarted = false; ESP_LOGW(TAG, "SNTP start failed"); }
}

void wifiEvent(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<const ip_event_got_ip_t *>(data);
        taskENTER_CRITICAL(&lock);
        snprintf(staIp, sizeof(staIp), IPSTR, IP2STR(&event->ip_info.ip));
        staReady = true;
        taskEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "STA connected | IP %s", staIp);
        startSntp();
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
               (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        taskENTER_CRITICAL(&lock);
        const bool was = staReady;
        staReady = false;
        taskEXIT_CRITICAL(&lock);
        if (was) ESP_LOGW(TAG, "STA disconnected; setup AP stays available");

    }
}

esp_err_t startWifi() {
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!sta || !ap) return ESP_ERR_NO_MEM;
    // Hostname must be set before either interface starts.
    esp_netif_set_hostname(sta, hostname);
    esp_netif_set_hostname(ap, hostname);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    wifi_config_t apCfg = {}, staCfg = {};
    memcpy(apCfg.ap.ssid, hostname, strlen(hostname));
    apCfg.ap.ssid_len = strlen(hostname);
    apCfg.ap.channel = 1;
    apCfg.ap.max_connection = 4;
    apCfg.ap.authmode = WIFI_AUTH_OPEN;
    // The driver arrays also accept a full-length SSID/PSK without a NUL.
    memcpy(staCfg.sta.ssid, settings.ssid, strlen(settings.ssid));
    memcpy(staCfg.sta.password, settings.password, strlen(settings.password));
    if ((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_AP, &apCfg)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &staCfg)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    esp_wifi_set_ps(WIFI_PS_NONE); // same as WiFi.setSleep(false): lower command latency
    return ESP_OK;
}

void resolveServer() {
    char host[65];
    strlcpy(host, settings.server, sizeof(host));
    for (char *p = host; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    const size_t len = strlen(host);
    char resolved[65] = {};
    struct in_addr literal;
    if (inet_aton(host, &literal)) {
        strlcpy(resolved, host, sizeof(resolved));
    } else if (len > 6 && !strcmp(host + len - 6, ".local")) {
        host[len - 6] = 0;
        esp_ip4_addr_t addr;
        if (mdns_query_a(host, 500, &addr) == ESP_OK) snprintf(resolved, sizeof(resolved), IPSTR, IP2STR(&addr));
    } else {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *result = nullptr;
        if (!getaddrinfo(host, nullptr, &hints, &result) && result) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)result->ai_addr)->sin_addr, resolved, sizeof(resolved));
            freeaddrinfo(result);
        }
    }
    if (resolved[0]) {
        taskENTER_CRITICAL(&lock);
        strlcpy(serverAddress, resolved, sizeof(serverAddress));
        taskEXIT_CRITICAL(&lock);
    } else {
        ESP_LOGW(TAG, "PC host lookup failed; retrying in 30s");
    }
}

void networkTask(void *) {
    int64_t lastResolve = 0;
    bool wasConnected = false;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        taskENTER_CRITICAL(&lock);
        const int64_t restart = restartAt;
        const bool connected = staReady;
        taskEXIT_CRITICAL(&lock);
        if (restart && now >= restart) esp_restart();
        if (connected && !wasConnected) lastResolve = now - 30000000; // resolve immediately
        wasConnected = connected;
        ard_wifi_tick();
        if (connected && now - lastResolve >= 30000000) {
            lastResolve = now;
            resolveServer();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void startMdns(const char *role) {
    if (mdns_init() != ESP_OK) { ESP_LOGW(TAG, "mDNS startup failed; use AP IP"); return; }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("ARD DriveMCU");
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    mdns_txt_item_t txt[] = {{"role", role}};
    mdns_service_add(nullptr, "_ard", "_tcp", 80, txt, 1);
    mdns_service_add(nullptr, "_ard-command", "_tcp", 5002, nullptr, 0);
}

esp_err_t redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/network");
    return httpd_resp_sendstr(req, "Open network settings");
}

esp_err_t startHttp() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&http, &config);
    if (err != ESP_OK) return err;
    httpd_uri_t root = {};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = redirect;
    httpd_register_uri_handler(http, &root);
    return ard_network_register_http(http);
}

} // namespace

const ard_network_settings_t *ard_network_settings(void) { return &settings; }
const char *ard_network_hostname(void) { return hostname; }
const char *ard_network_password(void) { return password; }
const char *ard_network_token(void) { return token; }

bool ard_network_connected(void) {
    taskENTER_CRITICAL(&lock);
    const bool ready = staReady;
    taskEXIT_CRITICAL(&lock);
    return ready;
}

void ard_network_server(char *out, size_t size) {
    taskENTER_CRITICAL(&lock);
    strlcpy(out, serverAddress, size);
    taskEXIT_CRITICAL(&lock);
}

void ard_network_ip(char *out, size_t size) {
    taskENTER_CRITICAL(&lock);
    strlcpy(out, staReady ? staIp : "0.0.0.0", size);
    taskEXIT_CRITICAL(&lock);
}

int ard_network_rssi(void) {
    wifi_ap_record_t ap;
    return ard_network_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

void ard_network_restart_later(void) {
    taskENTER_CRITICAL(&lock);
    restartAt = esp_timer_get_time() + 1500000;
    taskEXIT_CRITICAL(&lock);
}

void ard_network_print_setup(void) {
    ard_setup_qr_print();
    printf("[NET] AP: %s | Open Wi-Fi | No web login\n", hostname);
    printf("[NET] Setup: http://192.168.4.1/network or http://%s.local/network\n", hostname);
}

esp_err_t ard_network_start(const char *role, const char *legacyNamespace, const char *defaultServer) {
    settings.version = 1;
    strlcpy(settings.server, defaultServer, sizeof(settings.server));
    loadSettings(legacyNamespace);
    strlcpy(serverAddress, settings.server, sizeof(serverAddress));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    snprintf(hostname, sizeof(hostname), "ard-%s-%02x%02x%02x", role, mac[3], mac[4], mac[5]);
    password[0] = 0; // Open setup AP; no separate device password.
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifiEvent, nullptr));
    esp_err_t err = startWifi();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err)); return err; }
    if ((err = ard_wifi_init(&settings)) != ESP_OK) return err;
    startMdns(role);
    if ((err = startHttp()) != ESP_OK) ESP_LOGE(TAG, "Setup portal failed: %s", esp_err_to_name(err));
    ard_setup_qr_init(hostname, password);
    ard_network_print_setup();
    if (!settings.ssid[0]) ESP_LOGW(TAG, "No Wi-Fi saved yet: connect to the setup AP");
    return xTaskCreatePinnedToCore(networkTask, "ard_network", 4096, nullptr, 3, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
