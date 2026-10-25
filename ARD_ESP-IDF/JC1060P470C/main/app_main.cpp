// JC1060 HMI: onboard display/touch, ES8311 voice and BLE gamepad.
// Existing DriveMCU remains a separate network endpoint controlled through the PC.
#include <stdio.h>
#include "ard_network.h"
#include "board.h"
#include "bsp_display.h"
#include "c6_update.h"
#include "commands.h"
#include "controls.h"
#include "gamepad.h"
#include "onboard_audio.h"
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

static void healthTask(void *) {
    static const char *names[] = {"tcp_cmd", "console", "ard_network", "pc_link", "drive_input",
                                  "lvgl", "controls", "gamepad", "ui_settings", "voice_io", "voice_rx", "voice_upload"};
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        printf("[Health] heap=%lu min_heap=%lu stack_free:",
               (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size());
        for (const char *name : names) {
            TaskHandle_t task = xTaskGetHandle(name);
            if (task) printf(" %s=%u", name, (unsigned)uxTaskGetStackHighWaterMark(task));
        }
        printf("\n");
    }
}

static void startPortal() {
    httpd_handle_t server = nullptr;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) != ESP_OK || ard_network_register_http(server) != ESP_OK)
        ESP_LOGE(TAG, "Setup portal (port 80) failed");
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

    // An older ESP-Hosted C6 associates but never passes DHCP traffic; update it first.
    if ((err = c6_firmware_check()) != ESP_OK) ESP_LOGW(TAG, "[C6] Firmware check: %s", esp_err_to_name(err));

    // Wi-Fi goes through the C6 (esp_wifi_remote). A C6 without ESP-Hosted firmware ends here
    // with an error; the screen and the serial console keep working.
    if ((err = ard_network_start("jc1060", nullptr, CONFIG_ARD_DEFAULT_SERVER)) != ESP_OK)
        ESP_LOGE(TAG, "[NET] Network setup failed: %s", esp_err_to_name(err));
    startPortal();
    if ((err = command_servers_start()) != ESP_OK) fatal("command servers", err);
    if ((err = pc_link_start()) != ESP_OK) ESP_LOGE(TAG, "PC link failed: %s", esp_err_to_name(err));

    if ((err = gamepad_start()) != ESP_OK) ESP_LOGE(TAG, "Gamepad (BLE) unavailable: %s", esp_err_to_name(err));
    if ((err = controls_start()) != ESP_OK) fatal("controls", err);
    xTaskCreatePinnedToCore(healthTask, "health", 3072, nullptr, 1, nullptr, 0);

    onboard_audio_boot();
    command_print_help();
}
