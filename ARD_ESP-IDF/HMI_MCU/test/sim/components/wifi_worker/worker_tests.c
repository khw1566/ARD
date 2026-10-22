#define esp_err_to_name test_esp_err_to_name
#define esp_event_handler_register test_esp_event_handler_register
#define esp_timer_get_time test_esp_timer_get_time
#define esp_wifi_scan_start test_esp_wifi_scan_start
#define esp_wifi_scan_get_ap_records test_esp_wifi_scan_get_ap_records
#define esp_wifi_clear_ap_list test_esp_wifi_clear_ap_list
#define esp_wifi_set_config test_esp_wifi_set_config
#define esp_wifi_connect test_esp_wifi_connect
#define esp_wifi_disconnect test_esp_wifi_disconnect
#define nvs_open test_nvs_open
#define nvs_get_blob test_nvs_get_blob
#define nvs_set_blob test_nvs_set_blob
#define nvs_commit test_nvs_commit
#define nvs_close test_nvs_close
#define main wifi_test_entry
#include "../../../../../shared/test/test_wifi.c"
#undef main

static void reset_worker(void) {
    profiles = (ard_wifi_profiles_t){.version = 1};
    configured = (ard_wifi_profile_t){0}; active = configured;
    memset(results, 0, sizeof(results)); result_count = 0;
    initialized = scanning = connected = failed = got_ip = false; request_scan = true;
    scan_error = ESP_ERR_INVALID_STATE;
    connecting = remembered = pending_tried = history_dirty = configured_known = was_connected = false;
    tried = 0; attempt_at = next_scan = next_save = 0;
    disk = (ard_wifi_profiles_t){0}; memset(found, 0, sizeof(found)); found_count = 0;
    scans = connects = writes = commits = 0; clock_us = 0;
    scan_result = write_result = ESP_OK;
    last_config = (wifi_config_t){0};
}

void run_wifi_worker_tests(void) {
    const char *cases[] = {"slots", "visibility", "failed-new", "old-password", "timeout", "scan-error", "save-retry"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset_worker();
        char *args[] = {"wifi-test", (char *)cases[i]};
        assert(wifi_test_entry(2, args) == 0);
    }
}
