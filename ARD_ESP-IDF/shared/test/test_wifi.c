#include <assert.h>
#include <stdlib.h>
#include "wifi_test_sdk.h"
// Exercise the actual worker with deterministic Wi-Fi/NVS events.
#include "../ard_wifi.c"
#include "../ard_setup_qr.h"

static ard_wifi_profiles_t disk;
static struct { uint32_t version, count; ard_wifi_profile_t entries[16]; } legacy_disk;
static wifi_ap_record_t found[64];
static uint16_t found_count;
static int scans, connects, writes, commits;
static int64_t clock_us;
static esp_err_t scan_result, write_result;
static wifi_config_t last_config;
static int64_t delayed_disconnect_at;
static bool delay_disconnect, drop_disconnect;
void vTaskDelay(int ticks) {
    clock_us += ticks * 1000LL;
    if (delayed_disconnect_at && clock_us >= delayed_disconnect_at) {
        delayed_disconnect_at = 0;
        event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    }
}
const char *esp_err_to_name(esp_err_t err) { return "test"; }
esp_err_t esp_event_handler_register(esp_event_base_t b, int32_t id, esp_event_handler_t fn, void *a) { return ESP_OK; }
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c, bool b) { ++scans; return scan_result; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *out) {
    if (*n > found_count) *n = found_count;
    memcpy(out, found, *n * sizeof(*out)); return ESP_OK;
}
esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
esp_err_t esp_wifi_set_config(int iface, wifi_config_t *c) { last_config = *c; return ESP_OK; }
esp_err_t esp_wifi_connect(void) { ++connects; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) {
    if (drop_disconnect) return ESP_OK;
    if (delay_disconnect) delayed_disconnect_at = clock_us + 700000;
    else event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    return ESP_OK;
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle) { *handle = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *size) {
    if (!strcmp(k, "profiles16")) {
        if (!legacy_disk.version) return ESP_ERR_NVS_NOT_FOUND;
        assert(*size == sizeof(legacy_disk));
        memcpy(out, &legacy_disk, sizeof(legacy_disk)); return ESP_OK;
    }
    assert(!strcmp(k, "profiles8"));
    if (!disk.version) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size == sizeof(disk)); memcpy(out, &disk, sizeof(disk)); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *data, size_t size) {
    assert(!strcmp(k, "profiles8")); ++writes;
    if (write_result) return write_result;
    memcpy(&disk, data, size); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { ++commits; return ESP_OK; }
void nvs_close(nvs_handle_t h) { }

static ard_wifi_profile_t profile(const char *ssid, const char *pw) {
    ard_wifi_profile_t p = {0}; strcpy(p.ssid, ssid); strcpy(p.password, pw); return p;
}
static void visible_ap(const char *ssid) {
    strcpy((char *)found[found_count].ssid, ssid);
    found[found_count].rssi = -45; ++found_count;
}
static void begin(const char *ssid, const char *pw) {
    ard_network_settings_t s = {0}; s.version = 1; strcpy(s.server, "pc.local");
    strcpy(s.ssid, ssid); strcpy(s.password, pw); assert(ard_wifi_init(&s) == ESP_OK);
}
static void success(void) { event(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL); ard_wifi_tick(); }
static void failure(void) { event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL); ard_wifi_tick(); }

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "slots")) {
        ard_wifi_profiles_t p = {.version = 1};
        for (int i = 0; i < 9; ++i) {
            char ssid[33]; snprintf(ssid, sizeof(ssid), "SSID-%d", i);
            ard_wifi_profile_t v = profile(ssid, "password"); assert(ard_wifi_profiles_remember(&p, &v));
        }
        assert(p.count == 8 && !strcmp(p.entries[0].ssid, "SSID-8") && !strcmp(p.entries[7].ssid, "SSID-1"));
        ard_wifi_profile_t v = profile("SSID-7", "new-password");
        assert(ard_wifi_profiles_remember(&p, &v)); assert(p.count == 8);
        assert(!strcmp(p.entries[0].password, "new-password")); assert(ard_wifi_profiles_valid(&p));
        assert(!ard_wifi_profiles_remember(&p, &v));
        memset(v.ssid, 'a', sizeof(v.ssid)); assert(!ard_wifi_profile_valid(&v));
        p.count = 9; assert(!ard_wifi_profiles_valid(&p));
    } else if (!strcmp(argv[1], "migration") || !strcmp(argv[1], "invalid-migration")) {
        legacy_disk.version = 1; legacy_disk.count = 16;
        for (int i = 0; i < 16; ++i) {
            char ssid[33]; snprintf(ssid, sizeof(ssid), "legacy-%d", i);
            legacy_disk.entries[i] = profile(ssid, "password");
        }
        bool invalid = !strcmp(argv[1], "invalid-migration");
        if (invalid) memset(legacy_disk.entries[15].ssid, 'x', 33);
        visible_ap("legacy-0"); begin("", "");
        assert(profiles.count == (invalid ? 0 : 8));
        ard_wifi_tick();
        if (invalid) assert(connects == 0 && writes == 0);
        else {
            assert(!strcmp(profiles.entries[7].ssid, "legacy-7"));
            success(); assert(disk.count == 8 && writes == 1);
            ard_wifi_profile_t saved;
            assert(ard_wifi_saved_profile("legacy-7", &saved));
            assert(!strcmp(saved.password, "password"));
            assert(!ard_wifi_saved_profile("legacy-8", &saved));
        }
    } else if (!strcmp(argv[1], "visibility")) {
        disk.version = 1; disk.count = 2;
        disk.entries[0] = profile("absent", "password"); disk.entries[1] = profile("present", "password");
        visible_ap("present"); visible_ap("present"); visible_ap("unrelated"); begin("absent", "password");
        ard_wifi_tick(); assert(scans == 1 && connects == 1 && writes == 0);
        assert(!strcmp((char *)last_config.sta.ssid, "present"));
        assert(result_count == 2); success(); assert(writes == 1 && commits == 1 && disk.count == 2);
        assert(!strcmp(disk.entries[0].ssid, "present")); ard_wifi_tick(); assert(writes == 1);
        failure(); ard_wifi_tick(); assert(scans == 2); // Fresh scan after losing a successful connection.
    } else if (!strcmp(argv[1], "failed-new")) {
        visible_ap("new"); begin("new", "wrong-password"); ard_wifi_tick(); failure();
        assert(writes == 0 && profiles.count == 0);
        clock_us = 16000000; ard_wifi_tick(); ard_wifi_tick(); assert(scans == 2);
    } else if (!strcmp(argv[1], "old-password")) {
        disk.version = 1; disk.count = 1; disk.entries[0] = profile("same", "old-password");
        visible_ap("same"); begin("same", "new-password"); ard_wifi_tick();
        assert(!strcmp((char *)last_config.sta.password, "new-password")); failure();
        assert(connects == 2 && !strcmp((char *)last_config.sta.password, "old-password"));
    } else if (!strcmp(argv[1], "timeout")) {
        disk.version = 1; disk.count = 2;
        disk.entries[0] = profile("first", "password"); disk.entries[1] = profile("second", "password");
        visible_ap("first"); visible_ap("second"); begin("first", "password"); ard_wifi_tick();
        clock_us = 12000000; ard_wifi_tick(); ard_wifi_tick();
        assert(connects == 2 && scans == 1 && !strcmp((char *)last_config.sta.ssid, "second"));
    } else if (!strcmp(argv[1], "slow-dhcp")) {
        visible_ap("router"); begin("router", "password"); ard_wifi_tick();
        event(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
        clock_us = 13000000; ard_wifi_tick();
        assert(connecting && !failed && connects == 1);
        success(); assert(writes == 1);
    } else if (!strcmp(argv[1], "security-config")) {
        visible_ap("router"); begin("router", "password"); ard_wifi_tick();
        assert(last_config.sta.pmf_cfg.capable && !last_config.sta.pmf_cfg.required);
        assert(last_config.sta.sae_pwe_h2e == WPA3_SAE_PWE_BOTH);
    } else if (!strcmp(argv[1], "scan-error")) {
        scan_result = ESP_FAIL; visible_ap("first"); begin("first", "password"); ard_wifi_tick();
        assert(connects == 0 && writes == 0 && result_count == 0 && scan_error == ESP_FAIL);
    } else if (!strcmp(argv[1], "save-retry")) {
        visible_ap("new"); begin("new", "password"); ard_wifi_tick(); write_result = ESP_FAIL; success();
        assert(writes == 1 && commits == 0); ard_wifi_tick(); assert(writes == 1);
        clock_us = 5000000; write_result = ESP_OK; ard_wifi_tick(); assert(commits == 1 && disk.count == 1);
    } else if (!strcmp(argv[1], "manual-success")) {
        disk.version = 1; disk.count = 1; disk.entries[0] = profile("old", "old-password");
        visible_ap("old"); begin("old", "old-password"); ard_wifi_tick(); success();
        ard_wifi_profile_t previous;
        assert(ard_wifi_active_profile(&previous) && !strcmp(previous.ssid, "old"));
        ard_network_settings_t manual = {.version = 1};
        strcpy(manual.server, "pc.local"); strcpy(manual.ssid, "manual-hidden"); strcpy(manual.password, "new-password");
        esp_wifi_disconnect();
        assert(ard_wifi_begin_manual(&manual) == ESP_OK);
        assert(connects == 2 && scans == 1); // Manual hidden SSID does not require a visible scan result.
        success();
        assert(disk.count == 2 && !strcmp(disk.entries[0].ssid, "manual-hidden"));
        assert(!strcmp(disk.entries[0].password, "new-password"));
        assert(!strcmp(disk.entries[1].ssid, "old"));
    } else if (!strcmp(argv[1], "manual-rollback")) {
        disk.version = 1; disk.count = 1; disk.entries[0] = profile("old", "old-password");
        visible_ap("old"); begin("old", "old-password"); ard_wifi_tick(); success();
        ard_wifi_profile_t previous;
        assert(ard_wifi_active_profile(&previous));
        ard_network_settings_t manual = {.version = 1};
        strcpy(manual.server, "pc.local"); strcpy(manual.ssid, "new"); strcpy(manual.password, "wrong-password");
        esp_wifi_disconnect(); assert(ard_wifi_begin_manual(&manual) == ESP_OK);
        event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
        assert(disk.count == 1 && !strcmp(disk.entries[0].ssid, "old"));
        memcpy(manual.ssid, previous.ssid, sizeof(manual.ssid));
        memcpy(manual.password, previous.password, sizeof(manual.password));
        assert(ard_wifi_begin_manual(&manual) == ESP_OK); success();
        assert(disk.count == 1 && !strcmp(disk.entries[0].ssid, "old"));
        assert(!strcmp(disk.entries[0].password, "old-password"));
    } else if (!strcmp(argv[1], "manual-transient")) {
        disk.version = 1; disk.count = 1; disk.entries[0] = profile("old", "old-password");
        visible_ap("old"); begin("old", "old-password"); ard_wifi_tick(); success();
        ard_network_settings_t next = {.version = 1};
        strcpy(next.server, "pc.local"); strcpy(next.ssid, "new"); strcpy(next.password, "new-password");
        assert(ard_wifi_begin_manual(&next) == ESP_OK);
        int before = connects;
        failure(); assert(connects == before && disk.count == 1);
        clock_us += 1000000; ard_wifi_tick();
        assert(connects == before + 1 && !strcmp((char *)last_config.sta.ssid, "new"));
        failure(); clock_us += 1000000; ard_wifi_tick();
        assert(connects == before + 2);
        failure(); clock_us += 1000000; ard_wifi_tick();
        assert(connects == before + 2 && disk.count == 1); // Bounded; never switch to old during setup.
        ard_wifi_end_manual();
        ard_wifi_tick(); assert(!strcmp((char *)last_config.sta.ssid, "old"));
    } else if (!strcmp(argv[1], "manual-retry-success") || !strcmp(argv[1], "manual-lost-ip")) {
        begin("", "");
        ard_network_settings_t next = {.version = 1};
        strcpy(next.server, "pc.local"); strcpy(next.ssid, "new"); strcpy(next.password, "new-password");
        assert(ard_wifi_begin_manual(&next) == ESP_OK);
        if (!strcmp(argv[1], "manual-lost-ip")) {
            event(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
            event(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
            ard_wifi_tick(); assert(!associated && !connecting);
        } else failure();
        clock_us += 1000000; ard_wifi_tick(); assert(connects == 2);
        success(); ard_wifi_end_manual();
        assert(disk.count == 1 && !strcmp(disk.entries[0].ssid, "new"));
        assert(!strcmp(disk.entries[0].password, "new-password") && scans == 0);
    } else if (!strcmp(argv[1], "delayed-disconnect") || !strcmp(argv[1], "missing-disconnect")) {
        visible_ap("old"); begin("old", "old-password"); ard_wifi_tick(); success();
        delay_disconnect = true;
        drop_disconnect = !strcmp(argv[1], "missing-disconnect");
        ard_network_settings_t next = {.version = 1};
        strcpy(next.server, "pc.local"); strcpy(next.ssid, "new"); strcpy(next.password, "new-password");
        int before = connects;
        esp_err_t err = ard_wifi_begin_manual(&next);
        if (drop_disconnect) {
            assert(err == ESP_ERR_TIMEOUT && connects == before && clock_us >= 2500000);
            assert(!strcmp((char *)last_config.sta.ssid, "old"));
        } else {
            assert(err == ESP_OK && connects == before + 1 && clock_us >= 700000);
            assert(!failed && !delayed_disconnect_at);
            success();
            clock_us = 5000000; ard_wifi_tick(); // Respect history write throttling.
            assert(!strcmp(disk.entries[0].ssid, "new"));
        }
    } else if (!strcmp(argv[1], "manual-empty")) {
        disk.version = 1; disk.count = 1; disk.entries[0] = profile("old", "old-password");
        visible_ap("old"); begin("", "");
        ard_network_settings_t manual = {.version = 1}; strcpy(manual.server, "pc.local");
        assert(ard_wifi_begin_manual(&manual) == ESP_OK); ard_wifi_tick();
        assert(connects == 1 && !strcmp((char *)last_config.sta.ssid, "old"));
    } else if (!strcmp(argv[1], "qr")) {
        ard_setup_qr_init("ard-hmi-123456", "");
        assert(ard_qrcodegen_getSize(ard_setup_qr(true)->modules) > 0);
        assert(ard_qrcodegen_getSize(ard_setup_qr(false)->modules) > 0);
        assert(ard_qrcodegen_getModule(ard_setup_qr(true)->modules, 0, 0));
        assert(!ard_qrcodegen_getModule(ard_setup_qr(true)->modules, -1, 0));
        for (int kind = 0; kind < 2; ++kind) {
            char filename[24]; snprintf(filename, sizeof(filename), "qr-%d.pgm", kind);
            FILE *f = fopen(filename, "wb"); assert(f);
            const uint8_t *qr = ard_setup_qr(kind == 0)->modules;
            int n = ard_qrcodegen_getSize(qr), side = (n + 8) * 8;
            fprintf(f, "P5\n%d %d\n255\n", side, side);
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
                fputc(ard_qrcodegen_getModule(qr, x / 8 - 4, y / 8 - 4) ? 0 : 255, f);
            fclose(f);
        }
    } else assert(false);
    printf("PASS %s\n", argv[1]); return 0;
}
