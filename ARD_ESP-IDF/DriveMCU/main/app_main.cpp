// ARD DriveMCU firmware (ESP-IDF 5.5), replaces DriveMCU/src/main.cpp (PlatformIO/Arduino).
//
// Scheduling (FreeRTOS, 1 kHz tick):
//   core 1  step ISR (GPTimer 20 kHz, level 3)      pulse generation
//   core 1  motion   prio 22  1 kHz fixed rate       queue, trapezoid planner, rail PID
//   core 0  tcp_cmd  prio 10  TCP 5002 commands      console prio 9 (UART0)
//   core 0  sensors  prio 6   100 Hz fixed rate      sole I2C owner: AHT20/BMP280/ToF/OLED
//   core 0  gps      prio 6   UART2 NMEA             heartbeat 5, log 4, buzzer 3, network 3
//   core 0  health   prio 1   every 30 s             heap / stack watermarks
// motion, sensors and tcp_cmd are watched by the task watchdog.
#include <stdio.h>
#include "board.h"
#include "buzzer.h"
#include "commands.h"
#include "gps.h"
#include "motion.h"
#include "net/ard_network.h"
#include "sensors.h"
#include "telemetry.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "drive";

static void fatal(const char *what, esp_err_t err) {
    motion_emergency_stop();
    ESP_LOGE(TAG, "[FATAL] %s: %s; motors stopped, restarting in 5 s", what, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

static void healthTask(void *) {
    static const char *names[] = {"motion", "sensors", "tcp_cmd", "console", "gps", "heartbeat", "log", "buzzer", "ard_network"};
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
    motion_load_rail_settings();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if ((err = buzzer_start()) != ESP_OK) fatal("buzzer", err);
    if ((err = telemetry_start()) != ESP_OK) fatal("telemetry", err);
    if ((err = gps_start()) != ESP_OK) fatal("gps", err);
    if ((err = sensors_start()) != ESP_OK) fatal("sensors", err);
    if ((err = ard_network_start("drive", "drive-config", CONFIG_ARD_DEFAULT_SERVER)) != ESP_OK)
        ESP_LOGE(TAG, "[NET] Network setup failed: %s", esp_err_to_name(err)); // keep motion/serial alive
    if ((err = command_servers_start()) != ESP_OK) fatal("command servers", err);
    xTaskCreatePinnedToCore(healthTask, "health", 3072, nullptr, 1, nullptr, 0);

    buzzer_play(MELODY_BOOT);
    command_print_help();
}
