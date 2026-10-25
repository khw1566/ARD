#include "ard_wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "ARD_WIFI";
static ard_wifi_profiles_t profiles = {.version = 1};
static ard_wifi_profile_t configured, active;
static ard_wifi_ap_t results[ARD_WIFI_SCAN_LIMIT];
static size_t result_count;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool initialized, request_scan = true, scanning, connected, failed, got_ip;
static esp_err_t scan_error = ESP_ERR_INVALID_STATE;
static bool connecting, remembered, pending_tried, history_dirty, configured_known, was_connected;
static uint32_t tried;
static int64_t attempt_at, next_scan, next_save;
static bool associated, manual;
static uint32_t disconnect_generation, manual_retries;
static int64_t associated_at, manual_retry_at;

static void station_config(wifi_config_t *config, const ard_wifi_profile_t *profile) {
    memset(config, 0, sizeof(*config));
    memcpy(config->sta.ssid, profile->ssid, strlen(profile->ssid));
    memcpy(config->sta.password, profile->password, strlen(profile->password));
    config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    // Do not leave SAE derivation unspecified: explicitly accept both legacy
    // and H2E routers, without requiring WPA3/PMF on WPA2 or open networks.
    config->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config->sta.pmf_cfg.capable = true;
    config->sta.pmf_cfg.required = false;
}

// Preserve the most recent eight successes from firmware using profiles16.
static esp_err_t import_legacy(nvs_handle_t nvs) {
    typedef struct {
        uint32_t version, count;
        ard_wifi_profile_t entries[16];
    } legacy_profiles_t;
    legacy_profiles_t *old = calloc(1, sizeof(*old));
    if (!old) return ESP_ERR_NO_MEM;
    size_t size = sizeof(*old);
    esp_err_t err = nvs_get_blob(nvs, "profiles16", old, &size);
    if (err == ESP_OK) {
        if (size != sizeof(*old) || old->version != 1 || old->count > 16) err = ESP_FAIL;
        for (uint32_t i = 0; err == ESP_OK && i < old->count; ++i) {
            if (!ard_wifi_profile_valid(&old->entries[i])) { err = ESP_FAIL; break; }
            for (uint32_t j = 0; j < i; ++j)
                if (!strcmp(old->entries[i].ssid, old->entries[j].ssid)) err = ESP_FAIL;
        }
        if (err == ESP_OK) {
            profiles.count = old->count < ARD_WIFI_SLOTS ? old->count : ARD_WIFI_SLOTS;
            memcpy(profiles.entries, old->entries, profiles.count * sizeof(*profiles.entries));
            history_dirty = true; // Commit the migration after the next successful connection.
        }
    }
    free(old);
    return err;
}

static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    taskENTER_CRITICAL(&lock);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        associated = true; associated_at = esp_timer_get_time();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        associated = false; ++disconnect_generation;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) { connected = true; got_ip = true; failed = false; }
    if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
        (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) { connected = false; failed = true; }
    taskEXIT_CRITICAL(&lock);
}

esp_err_t ard_wifi_init(const ard_network_settings_t *s) {
    memcpy(configured.ssid, s->ssid, sizeof(configured.ssid));
    memcpy(configured.password, s->password, sizeof(configured.password));
    nvs_handle_t nvs;
    if (nvs_open("ard-network", NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = sizeof(profiles);
        esp_err_t err = nvs_get_blob(nvs, "profiles8", &profiles, &size);
        if (err == ESP_OK && (size != sizeof(profiles) || !ard_wifi_profiles_valid(&profiles))) err = ESP_FAIL;
        if (err == ESP_ERR_NVS_NOT_FOUND) err = import_legacy(nvs);
        if (err != ESP_OK) {
            memset(&profiles, 0, sizeof(profiles)); profiles.version = 1;
            if (err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "Ignoring invalid Wi-Fi history");
        }
        nvs_close(nvs);
    }
    for (uint32_t i = 0; i < profiles.count; ++i)
        if (!strcmp(configured.ssid, profiles.entries[i].ssid) &&
            !strcmp(configured.password, profiles.entries[i].password)) configured_known = true;
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, event, NULL);
    initialized = err == ESP_OK;
    ESP_LOGI(TAG, "Loaded %lu/8 successful Wi-Fi profiles", (unsigned long)profiles.count);
    return err;
}

void ard_wifi_request_scan(void) {
    taskENTER_CRITICAL(&lock);
    request_scan = true;
    taskEXIT_CRITICAL(&lock);
}

size_t ard_wifi_scan_snapshot(ard_wifi_ap_t *out, size_t capacity, bool *busy, esp_err_t *error) {
    taskENTER_CRITICAL(&lock);
    size_t count = result_count < capacity ? result_count : capacity;
    memcpy(out, results, count * sizeof(*out));
    *busy = initialized && (scanning || request_scan);
    *error = scan_error;
    taskEXIT_CRITICAL(&lock);
    return count;
}

bool ard_wifi_active_profile(ard_wifi_profile_t *out) {
    if (!out) return false;
    taskENTER_CRITICAL(&lock);
    bool valid = initialized && (connected || connecting) && active.ssid[0];
    if (valid) *out = active;
    taskEXIT_CRITICAL(&lock);
    return valid;
}

bool ard_wifi_saved_profile(const char *ssid, ard_wifi_profile_t *out) {
    if (!ssid || !out) return false;
    bool found = false;
    taskENTER_CRITICAL(&lock);
    for (uint32_t i = 0; i < profiles.count; ++i) {
        if (!strcmp(ssid, profiles.entries[i].ssid)) {
            *out = profiles.entries[i]; found = true; break;
        }
    }
    taskEXIT_CRITICAL(&lock);
    return found;
}

esp_err_t ard_wifi_begin_manual(const ard_network_settings_t *s) {
    if (!initialized || !s) return ESP_ERR_INVALID_STATE;
    if (s->version != 1 || !memchr(s->ssid, 0, sizeof(s->ssid)) ||
        !memchr(s->password, 0, sizeof(s->password)) || (s->ssid[0] && !ard_network_valid(s)))
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = ard_wifi_disconnect_wait();
    if (err != ESP_OK) return err;
    taskENTER_CRITICAL(&lock);
    memcpy(configured.ssid, s->ssid, sizeof(configured.ssid));
    memcpy(configured.password, s->password, sizeof(configured.password));
    active = configured;
    connected = false; got_ip = false; failed = false; was_connected = false;
    connecting = s->ssid[0] != 0;
    manual = connecting; manual_retries = 0; manual_retry_at = 0;
    remembered = false; configured_known = false; tried = 0; pending_tried = true;
    request_scan = !connecting;
    attempt_at = esp_timer_get_time();
    taskEXIT_CRITICAL(&lock);
    // With no previous SSID, resume normal history selection on the next tick.
    if (!s->ssid[0]) return ESP_OK;
    wifi_config_t config;
    station_config(&config, &active);
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        taskENTER_CRITICAL(&lock);
        connecting = false;
        manual = false; request_scan = true;
        taskEXIT_CRITICAL(&lock);
    }
    return err;
}

void ard_wifi_end_manual(void) {
    // The caller keeps serial ownership until settings have been stored or the
    // previous profile restored. Background selection can then resume.
    manual = false;
}

esp_err_t ard_wifi_disconnect_wait(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&lock);
    bool pending = connected || associated || (connecting && !failed);
    uint32_t generation = disconnect_generation;
    taskEXIT_CRITICAL(&lock);
    if (pending) {
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) return err;
        if (err == ESP_OK) {
            const int64_t deadline = esp_timer_get_time() + 2500000LL;
            bool done;
            do {
                taskENTER_CRITICAL(&lock);
                done = disconnect_generation != generation;
                taskEXIT_CRITICAL(&lock);
                if (done) break;
                vTaskDelay(pdMS_TO_TICKS(20));
            } while (esp_timer_get_time() < deadline);
            if (!done) return ESP_ERR_TIMEOUT;
        }
    }
    taskENTER_CRITICAL(&lock);
    connected = associated = connecting = got_ip = failed = was_connected = false;
    manual = false;
    taskEXIT_CRITICAL(&lock);
    return ESP_OK;
}

static bool visible(const char *ssid) {
    for (size_t i = 0; i < result_count; ++i) if (!strcmp(ssid, results[i].ssid)) return true;
    return false;
}

static void scan(void) {
    wifi_scan_config_t config = {0};
    // Short dwell preserves AP responsiveness while visiting other channels.
    config.scan_time.active.min = 60;
    config.scan_time.active.max = 120;
    taskENTER_CRITICAL(&lock);
    request_scan = false; scanning = true;
    taskEXIT_CRITICAL(&lock);
    esp_err_t err = esp_wifi_scan_start(&config, true);
    // Heap storage keeps network task stacks small on both ESP32 and P4.
    wifi_ap_record_t *records = NULL;
    uint16_t count = ARD_WIFI_SCAN_LIMIT;
    if (err == ESP_OK) {
        records = calloc(count, sizeof(*records));
        if (!records) { esp_wifi_clear_ap_list(); err = ESP_ERR_NO_MEM; }
        else {
            err = esp_wifi_scan_get_ap_records(&count, records);
            if (err != ESP_OK) esp_wifi_clear_ap_list();
        }
    }
    taskENTER_CRITICAL(&lock);
    result_count = 0;
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            char ssid[33];
            memcpy(ssid, records[i].ssid, 32); ssid[32] = 0;
            if (!ssid[0] || visible(ssid)) continue;
            ard_wifi_ap_t *ap = &results[result_count++];
            memcpy(ap->ssid, ssid, sizeof(ssid));
            ap->rssi = records[i].rssi;
            ap->open = records[i].authmode == WIFI_AUTH_OPEN;
            ap->saved = false;
            ap->password[0] = 0;
            for (uint32_t j = 0; j < profiles.count; ++j) {
                if (!strcmp(ssid, profiles.entries[j].ssid)) {
                    ap->saved = true;
                    memcpy(ap->password, profiles.entries[j].password, sizeof(ap->password));
                }
            }
        }
    }
    scan_error = err; scanning = false;
    taskEXIT_CRITICAL(&lock);
    free(records);
    if (err != ESP_OK) ESP_LOGW(TAG, "SSID scan failed: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "SSID scan: %u networks", (unsigned)result_count);
    next_scan = esp_timer_get_time() + 15000000;
    tried = 0; pending_tried = false;
}

static void remember(void) {
    if (!active.ssid[0] || remembered) return;
    taskENTER_CRITICAL(&lock);
    history_dirty |= ard_wifi_profiles_remember(&profiles, &active);
    taskEXIT_CRITICAL(&lock);
    if (!history_dirty) { remembered = true; return; }
    if (esp_timer_get_time() < next_save) return;
    next_save = esp_timer_get_time() + 5000000;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ard-network", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, "profiles8", &profiles, sizeof(profiles));
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    remembered = err == ESP_OK;
    if (remembered) history_dirty = false;
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi history save failed: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "Successful connection saved: %s (%lu/8)", active.ssid, (unsigned long)profiles.count);
}

void ard_wifi_tick(void) {
    if (!initialized) return;
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&lock);
    bool ready = connected, disconnected = failed, success = got_ip, requested = request_scan;
    bool linked = associated;
    int64_t linked_at = associated_at;
    failed = false; got_ip = false;
    taskEXIT_CRITICAL(&lock);
    if (ready) {
        was_connected = true;
        connecting = false;
        if (success || !remembered) remember();
        if (requested && !manual) scan();
        return;
    }
    if (manual) {
        // Live setup owns this SSID until its deadline. A transient failure must
        // retry that SSID, rather than silently switch to a history entry.
        if (disconnected) {
            if (linked) {
                // LOST_IP can arrive while the radio is still associated.
                // A new connect call must wait for a real disconnect first.
                esp_err_t err = ard_wifi_disconnect_wait();
                manual = true;
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Manual disconnect after IP loss failed: %s", esp_err_to_name(err));
                    return;
                }
            }
            connecting = false;
            if (manual_retries < 2) manual_retry_at = now + 1000000;
        }
        if (!connecting && manual_retry_at && now >= manual_retry_at) {
            manual_retry_at = 0; ++manual_retries;
            esp_err_t err = esp_wifi_connect();
            connecting = err == ESP_OK; attempt_at = esp_timer_get_time();
            if (err != ESP_OK && manual_retries < 2) manual_retry_at = attempt_at + 1000000;
            ESP_LOGI(TAG, "Manual connection retry %lu: %s", (unsigned long)manual_retries, esp_err_to_name(err));
        }
        return;
    }
    if (was_connected) {
        if (ard_wifi_disconnect_wait() != ESP_OK) return;
        ard_wifi_request_scan(); // Refresh visibility after disconnect has completed.
        return;
    }
    if (connecting) {
        // Association/authentication and DHCP have separate deadlines. Do not
        // tear down a good radio link just because DHCP took over 12 seconds.
        if (!disconnected && (linked ? now - linked_at < 30000000LL : now - attempt_at < 12000000LL)) return;
        // Wait for disconnect completion before starting another scan/connection.
        if (!disconnected) {
            esp_err_t err = ard_wifi_disconnect_wait();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Disconnect before next profile failed: %s", esp_err_to_name(err));
                return;
            }
        }
        connecting = false;
        ESP_LOGW(TAG, "Connection failed: %s; trying another visible saved SSID", active.ssid);
    }
    if (requested || !next_scan) scan();
    ard_wifi_profile_t *candidate = NULL;
    // A newly submitted or legacy setting gets one visible attempt per scan.
    // It only enters the eight-slot history when it obtains an IP address.
    if (!configured_known && !pending_tried && configured.ssid[0] && visible(configured.ssid)) {
        pending_tried = true;
        candidate = &configured;
    }
    for (uint32_t i = 0; !candidate && i < profiles.count; ++i) {
        if (!(tried & (1UL << i)) && visible(profiles.entries[i].ssid)) {
            tried |= 1UL << i; candidate = &profiles.entries[i];
        }
    }
    if (!candidate) {
        if (now >= next_scan) ard_wifi_request_scan();
        return;
    }
    taskENTER_CRITICAL(&lock);
    active = *candidate;
    taskEXIT_CRITICAL(&lock);
    wifi_config_t config;
    station_config(&config, &active);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    taskENTER_CRITICAL(&lock);
    failed = false; // Discard disconnects completed during the blocking scan.
    taskEXIT_CRITICAL(&lock);
    if (err == ESP_OK) err = esp_wifi_connect();
    connecting = err == ESP_OK; remembered = false; attempt_at = esp_timer_get_time();
    if (err != ESP_OK) ESP_LOGW(TAG, "Connect %s failed: %s", active.ssid, esp_err_to_name(err));
    else ESP_LOGI(TAG, "Connecting to scanned SSID: %s", active.ssid);
}
