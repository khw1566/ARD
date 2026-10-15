#include "ard_network.h"
#include "ard_config.h"
#include "ard_sender.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mdns.h"
#include "nvs.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

static const char *TAG = "ARD_NET";
static ard_network_settings_t settings = {.version = 1, .server = ARD_SERVER_HOST};
static char hostname[48], password[16], token[33], server_address[65];
static bool eth_ready, sta_ready;
static char eth_ip[16], sta_ip[16];
static int64_t restart_at;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

const ard_network_settings_t *ard_network_settings(void) { return &settings; }
const char *ard_network_hostname(void) { return hostname; }
const char *ard_network_password(void) { return password; }
const char *ard_network_token(void) { return token; }
bool ard_network_connected(void) {
    taskENTER_CRITICAL(&state_lock);
    bool ready = eth_ready || sta_ready;
    taskEXIT_CRITICAL(&state_lock);
    return ready;
}
void ard_network_server(char *out, size_t size) {
    taskENTER_CRITICAL(&state_lock);
    strlcpy(out, server_address, size);
    taskEXIT_CRITICAL(&state_lock);
}
void ard_network_restart_later(void) {
    taskENTER_CRITICAL(&state_lock);
    restart_at = esp_timer_get_time() + 1500000;
    taskEXIT_CRITICAL(&state_lock);
}

// All interface events run on the default event loop; Ethernet takes priority.
static void publish_network(void) {
    const char *ip = eth_ready ? eth_ip : sta_ip;
    esp_netif_t *preferred = esp_netif_get_handle_from_ifkey(eth_ready ? "ETH_DEF" : "WIFI_STA_DEF");
    if ((eth_ready || sta_ready) && preferred) esp_netif_set_default_netif(preferred);
    ard_sender_network(eth_ready || sta_ready, ip);
}
void ard_network_eth(bool ready, const char *ip) {
    taskENTER_CRITICAL(&state_lock);
    eth_ready = ready;
    if (ready) strlcpy(eth_ip, ip, sizeof(eth_ip));
    taskEXIT_CRITICAL(&state_lock);
    publish_network();
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        taskENTER_CRITICAL(&state_lock);
        snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        sta_ready = true;
        taskEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "Wi-Fi connected: %s", sta_ip);
        publish_network();
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
               (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        taskENTER_CRITICAL(&state_lock);
        sta_ready = false;
        taskEXIT_CRITICAL(&state_lock);
        publish_network();
        ESP_LOGW(TAG, "Wi-Fi disconnected; setup AP stays available");
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) {
        ard_network_eth(false, NULL);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && settings.ssid[0]) {
        esp_wifi_connect();
    }
}

static esp_err_t start_wifi(void) {
    // ESP-Hosted talks to the module's C6 over SDIO; pins are in sdkconfig.defaults.
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!sta || !ap) return ESP_ERR_NO_MEM;
    esp_netif_set_hostname(sta, hostname);
    esp_netif_set_hostname(ap, hostname);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    wifi_config_t ap_cfg = {0}, sta_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, hostname, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(hostname);
    strlcpy((char *)ap_cfg.ap.password, password, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    strlcpy((char *)sta_cfg.sta.ssid, settings.ssid, sizeof(sta_cfg.sta.ssid));
    // Wi-Fi driver's arrays also accept a full-length SSID/PSK without a NUL.
    memcpy(sta_cfg.sta.ssid, settings.ssid, strlen(settings.ssid));
    memcpy(sta_cfg.sta.password, settings.password, strlen(settings.password));
    if ((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "AP: %s | Password: %s | Login: admin", hostname, password);
    ESP_LOGI(TAG, "Setup: http://192.168.4.1/network or http://%s.local/network", hostname);
    return ESP_OK;
}

static void resolve_server(void) {
    esp_ip4_addr_t addr;
    char host[65];
    strlcpy(host, settings.server, sizeof(host));
    size_t len = strlen(host);
    char resolved[65] = {0};
    if (len > 6 && !strcasecmp(host + len - 6, ".local")) {
        host[len - 6] = 0;
        if (mdns_query_a(host, 500, &addr) == ESP_OK)
            snprintf(resolved, sizeof(resolved), IPSTR, IP2STR(&addr));
    } else {
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *result = NULL;
        if (!getaddrinfo(host, NULL, &hints, &result) && result) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)result->ai_addr)->sin_addr, resolved, sizeof(resolved));
            freeaddrinfo(result);
        }
    }
    if (resolved[0]) {
        taskENTER_CRITICAL(&state_lock);
        strlcpy(server_address, resolved, sizeof(server_address));
        taskEXIT_CRITICAL(&state_lock);
    } else ESP_LOGW(TAG, "PC host lookup failed; retry in 30s");
}

static void network_task(void *arg) {
    esp_err_t err = start_wifi();
    if (err != ESP_OK) ESP_LOGE(TAG, "C6 Wi-Fi init failed: %s; Ethernet setup page remains available", esp_err_to_name(err));
    int64_t last_retry = 0, last_resolve = -30000000;
    while (true) {
        int64_t now = esp_timer_get_time();
        taskENTER_CRITICAL(&state_lock);
        int64_t restart = restart_at;
        bool wifi_ready = sta_ready;
        taskEXIT_CRITICAL(&state_lock);
        if (restart && now >= restart) esp_restart();
        if (err == ESP_OK && !wifi_ready && settings.ssid[0] && now - last_retry >= 15000000) {
            last_retry = now;
            esp_wifi_connect();
        }
        if (ard_network_connected() && now - last_resolve >= 30000000) {
            last_resolve = now;
            resolve_server();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ard_network_start(void) {
    nvs_handle_t nvs;
    if (nvs_open("ard-network", NVS_READONLY, &nvs) == ESP_OK) {
        ard_network_settings_t stored = {0};
        size_t size = sizeof(stored);
        if (nvs_get_blob(nvs, "settings", &stored, &size) == ESP_OK &&
            size == sizeof(stored) && ard_network_valid(&stored)) settings = stored;
        nvs_close(nvs);
    }
    strlcpy(server_address, settings.server, sizeof(server_address));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    snprintf(hostname, sizeof(hostname), "ard-image-p4-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(password, sizeof(password), "ARD-%02x%02x%02x!", mac[3], mac[4], mac[5]);
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(hostname);
        mdns_instance_name_set("ARD P4 camera and Wi-Fi setup");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        mdns_service_add(NULL, "_ard", "_tcp", 80, NULL, 0);
    } else ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
    ESP_LOGI(TAG, "mDNS: http://%s.local/network | admin / %s", hostname, password);
    configASSERT(xTaskCreate(network_task, "ard_network", 6144, NULL, 3, NULL) == pdPASS);
}
