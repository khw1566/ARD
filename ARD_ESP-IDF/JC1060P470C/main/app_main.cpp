// JC1060 HMI: onboard display/touch and ES8311 voice.
// Existing DriveMCU remains a separate network endpoint controlled through the PC.
#include <stdio.h>
#include "ard_network.h"
#include "board.h"
#include "bsp_display.h"
#include "c6_update.h"
#include "commands.h"
#include "clock_sync.h"
#include "onboard_audio.h"
#include "visitor_voice.h"
#include "voice_upload_http.h"
#include "runtime_health.h"
#include "ui_text.h"
#include "pc_link.h"
#include "ui.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "ard";

static void fatal(const char *what, esp_err_t err) {
    ESP_LOGE(TAG, "[FATAL] %s: %s; restarting in 5 s", what, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

static void startPortal() {
    httpd_handle_t server = nullptr;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;
    config.stack_size = 6144;
    if (httpd_start(&server, &config) != ESP_OK || ard_network_register_http(server) != ESP_OK)
        ESP_LOGE(TAG, "Setup portal (port 80) failed");
    else if (voice_upload_http_register(server) != ESP_OK) ESP_LOGE(TAG, "Voice upload routes failed");
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "[Boot] HMI role reset=%d heap=%lu; DriveMCU stays independent",
             (int)esp_reset_reason(), (unsigned long)esp_get_free_heap_size());
    esp_err_t err;
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) fatal("nvs", err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    i2c_master_bus_handle_t i2c = nullptr;
    if ((err = bsp_i2c_init(&i2c)) != ESP_OK) fatal("i2c", err);
    // Keep the serial console/network available if display initialization fails.
    if ((err = bsp_display_start(i2c)) != ESP_OK) ESP_LOGE(TAG, "Display failed: %s", esp_err_to_name(err));
    else if ((err = ui_start()) != ESP_OK) ESP_LOGE(TAG, "UI failed: %s", esp_err_to_name(err));

    if ((err = onboard_audio_start(i2c)) != ESP_OK)
        ESP_LOGE(TAG, "Onboard audio failed: %s (UI/network remain available)", esp_err_to_name(err));

    if ((err = clock_sync_start()) != ESP_OK) ESP_LOGW(TAG, "Clock initialization: %s", esp_err_to_name(err));
    // An older ESP-Hosted C6 associates but never passes DHCP traffic; update it first.
    if ((err = c6_firmware_check()) != ESP_OK) ESP_LOGW(TAG, "[C6] Firmware check: %s", esp_err_to_name(err));

    // Wi-Fi goes through the C6 (esp_wifi_remote). A C6 without ESP-Hosted firmware ends here
    // with an error; the screen and the serial console keep working.
    if ((err = ard_network_start("jc1060", nullptr, CONFIG_ARD_DEFAULT_SERVER)) != ESP_OK)
        ESP_LOGE(TAG, "[NET] Network setup failed: %s", esp_err_to_name(err));
    startPortal();
    if ((err = command_servers_start()) != ESP_OK) fatal("command servers", err);
    if ((err = pc_link_start()) != ESP_OK) ESP_LOGE(TAG, "PC link failed: %s", esp_err_to_name(err));

    if ((err = runtime_health_start()) != ESP_OK)
        ESP_LOGW(TAG, "Scheduler diagnostics failed: %s", esp_err_to_name(err));
    if ((err = visitor_voice_start()) != ESP_OK)
        ESP_LOGW(TAG, "SD guidance failed: %s", esp_err_to_name(err));

    onboard_audio_boot();
    visitor_voice_request(VisitorVoice::Welcome, ui_language() == UI_LANG_KO);
    command_print_help();
}
