// ARD JC1060P470C firmware (ESP-IDF 5.5): one ESP32-P4 board replaces HMI_MCU (touch screen)
// and DriveMCU (steppers, sensors, GPS), and a BLE gamepad replaces the ControlMCU joystick.
// The on-board ESP32-C6 runs ESP-Hosted 2.12.2 and gives the P4 Wi-Fi and Bluetooth over SDIO.
//
// Scheduling (FreeRTOS, 1 kHz tick):
//   core 1  step ISR (GPTimer 20 kHz)      pulse generation
//   core 1  motion   prio 22  1 kHz         queue, trapezoid planner, rail PID, jog dead-man
//   core 0  tcp_cmd 10, console 9           TCP 5002 / UART0 commands (DriveMCU protocol)
//   core 0  controls 7  50 Hz               gamepad -> jog / commands
//   core 0  sensors 6, gps 6                AHT20/BMP280/ToF on the shared I2C bus, NEO-6M
//   core 0  heartbeat 5, log 4, lvgl 4      PC telemetry (as DriveMCU), UI
//   core 0  pc_link 3, gamepad 3, buzzer 3  PC status / commands (as HMI_MCU), BLE scan/connect
#include <stdio.h>
#include "ard_network.h"
#include "board.h"
#include "bsp_display.h"
#include "buzzer.h"
#include "commands.h"
#include "controls.h"
#include "gamepad.h"
#include "gps.h"
#include "motion.h"
#include "pc_link.h"
#include "sensors.h"
#include "telemetry.h"
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
    motion_emergency_stop();
    ESP_LOGE(TAG, "[FATAL] %s: %s; motors stopped, restarting in 5 s", what, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

static void healthTask(void *) {
    static const char *names[] = {"motion", "sensors", "tcp_cmd", "console", "gps", "heartbeat", "log",
                                  "buzzer", "ard_network", "pc_link", "lvgl", "controls", "gamepad"};
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        printf("[Health] heap=%lu min_heap=%lu logs_dropped=%lu stack_free:",
               (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(),
               (unsigned long)telemetry_dropped_logs());
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
    ESP_LOGI(TAG, "[Boot] reset=%d heap=%lu ToF=%s", (int)esp_reset_reason(), (unsigned long)esp_get_free_heap_size(),
             ARD_TOF_ENABLED ? "enabled" : "disabled (open loop)");

    // Motor drivers are disabled before anything else can fail.
    esp_err_t err = motion_start();
    if (err != ESP_OK) fatal("motion", err);

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
    // The screen is not needed for motion: a display failure is logged and the machine keeps running.
    if ((err = bsp_display_start(i2c)) != ESP_OK) ESP_LOGE(TAG, "Display failed: %s", esp_err_to_name(err));
    else if ((err = ui_start()) != ESP_OK) ESP_LOGE(TAG, "UI failed: %s", esp_err_to_name(err));

    if ((err = buzzer_start()) != ESP_OK) fatal("buzzer", err);
    if ((err = telemetry_start()) != ESP_OK) fatal("telemetry", err);
    if ((err = gps_start()) != ESP_OK) fatal("gps", err);
    if ((err = sensors_start(i2c)) != ESP_OK) fatal("sensors", err);

    // Wi-Fi goes through the C6 (esp_wifi_remote). A C6 without ESP-Hosted firmware ends here
    // with an error; motion, the screen and the serial console keep working.
    if ((err = ard_network_start("jc1060", nullptr, CONFIG_ARD_DEFAULT_SERVER)) != ESP_OK)
        ESP_LOGE(TAG, "[NET] Network setup failed: %s", esp_err_to_name(err));
    startPortal();
    if ((err = command_servers_start()) != ESP_OK) fatal("command servers", err);
    if ((err = pc_link_start()) != ESP_OK) ESP_LOGE(TAG, "PC link failed: %s", esp_err_to_name(err));

    if ((err = gamepad_start()) != ESP_OK) ESP_LOGE(TAG, "Gamepad (BLE) unavailable: %s", esp_err_to_name(err));
    if ((err = controls_start()) != ESP_OK) fatal("controls", err);
    xTaskCreatePinnedToCore(healthTask, "health", 3072, nullptr, 1, nullptr, 0);

    buzzer_play(MELODY_BOOT);
    command_print_help();
}
