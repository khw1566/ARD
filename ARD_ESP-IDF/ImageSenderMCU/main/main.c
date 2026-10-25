// ARD ImageSenderMCU (ESP32-S3 N16R8, ESP-IDF 5.5)
// Replaces ARD_software/ImageSenderMCU (Arduino). Module index:
//   camera.c     camera init / live quality / fresh frame grab
//   img_tx.c     reliable UDP image transfer (ARD image protocol v2)
//   pc_link.c    HTTP config poll, heartbeat, log, TCP-mode /upload
//   cmd_server.c TCP 5003 immediate capture trigger
//   components/ard_net   Wi-Fi AP+STA, mDNS, /network settings page
//   components/ard_proto protocol header + CRC32 shared with ARD_MAIN
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "ard_network.h"
#include "app_config.h"
#include "camera.h"
#include "cmd_server.h"
#include "img_tx.h"
#include "pc_link.h"

static const char *TAG = "MAIN";
static TaskHandle_t capture_task_handle;
static volatile uint32_t last_capture_ms, last_tx_ms, frames_ok, frames_failed;

// Any source may request a capture; requests that arrive while one is running
// collapse into a single follow-up capture (task notification is a counter we clear).
static void request_capture(const char *source) {
    ESP_LOGI(TAG, "Capture requested (%s)", source);
    if (capture_task_handle) xTaskNotifyGive(capture_task_handle);
}

static void stats_text(char *out, int size) {
    snprintf(out, size, "cap_ms=%lu,tx_ms=%lu,ok=%lu,fail=%lu", (unsigned long)last_capture_ms,
             (unsigned long)last_tx_ms, (unsigned long)frames_ok, (unsigned long)frames_failed);
}

static void capture_and_send(const pc_config_t *cfg) {
    if (!ard_network_connected()) {
        ESP_LOGW(TAG, "Capture skipped: Wi-Fi not connected");
        return;
    }
    int64_t t0 = esp_timer_get_time();
    camera_fb_t *fb = camera_capture_fresh();
    if (!fb) {
        frames_failed++;
        ESP_LOGE(TAG, "Capture failed: no frame from camera");
        pc_link_log("Capture failed: no frame from camera (sensor %s)", camera_sensor_name());
        return;
    }
    last_capture_ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "Captured %u bytes %ux%u in %lu ms, mode %s", (unsigned)fb->len, fb->width, fb->height,
             (unsigned long)last_capture_ms, cfg->tcp_mode ? "TCP" : "UDP");
    char host[65];
    ard_network_server(host, sizeof(host));
    int64_t t1 = esp_timer_get_time();
    bool ok;
    char detail[160];
    if (cfg->tcp_mode) {
        ok = pc_link_upload_jpeg(fb->buf, fb->len);
        snprintf(detail, sizeof(detail), "HTTP upload %s", ok ? "ok" : "failed");
    } else {
        img_tx_result_t r;
        ok = img_tx_send(host, APP_IMG_UDP_PORT, fb->buf, fb->len, &r);
        if (ok)
            snprintf(detail, sizeof(detail), "UDP v2 frame %u: %u chunks, %lu resent, %lu polls", r.frame_id, r.chunks,
                     (unsigned long)r.retransmits, (unsigned long)r.polls);
        else
            snprintf(detail, sizeof(detail), "UDP v2 frame %u failed: %s", r.frame_id, r.error);
    }
    last_tx_ms = (esp_timer_get_time() - t1) / 1000;
    size_t len = fb->len;
    esp_camera_fb_return(fb);
    if (ok) frames_ok++;
    else frames_failed++;
    pc_link_log("%s | %u bytes, cap %lu ms, tx %lu ms", detail, (unsigned)len, (unsigned long)last_capture_ms,
                (unsigned long)last_tx_ms);
}

static void capture_task(void *arg) {
    pc_config_t cfg;
    pc_link_get_config(&cfg);
    camera_start(cfg.resolution, cfg.quality);
    while (true) {
        // Wake on a capture request, or every 500 ms to apply PC config changes.
        bool requested = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) > 0;
        pc_link_get_config(&cfg);
        if (camera_apply(cfg.resolution, cfg.quality) != ESP_OK) {
            if (requested) pc_link_log("Capture skipped: camera not ready (%s)", cfg.resolution);
            vTaskDelay(pdMS_TO_TICKS(2000)); // retry init without a busy loop
            continue;
        }
        if (requested) capture_and_send(&cfg);
    }
}

static void console_task(void *arg) {
    // UART0 is the USB-UART port on the N16R8 board (same as the Arduino build).
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "Serial commands: c=capture, w=network setup, s=stats");
    uint8_t ch;
    while (true) {
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == 'c') request_capture("serial");
        else if (ch == 'w') ard_network_print_setup();
        else if (ch == 's') {
            char stats[96];
            stats_text(stats, sizeof(stats));
            ESP_LOGI(TAG, "%s, heap %lu, psram free %u", stats, (unsigned long)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
}

static void start_http(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) ESP_ERROR_CHECK(ard_network_register_http(server));
    else ESP_LOGE(TAG, "Settings web server failed to start");
}

void app_main(void) {
    ESP_LOGI(TAG, "=== ARD ImageSenderMCU (ESP-IDF) ===");
    ESP_LOGI(TAG, "Reset reason %d, heap %lu, PSRAM %u bytes", esp_reset_reason(),
             (unsigned long)esp_get_free_heap_size(), (unsigned)esp_psram_get_size());
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS reformat (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ard_network_start(APP_NET_ROLE, APP_DEFAULT_SERVER);
    start_http();
    // The capture task reads pc_link config/log right away, so its locks must exist first.
    pc_link_init();
    // Capture task owns the camera; it must exist before any trigger source starts.
    configASSERT(xTaskCreatePinnedToCore(capture_task, "capture", 8192, NULL, 5, &capture_task_handle, 1) == pdPASS);
    pc_link_start(request_capture);
    cmd_server_start(request_capture, stats_text);
    configASSERT(xTaskCreate(console_task, "console", 3072, NULL, 2, NULL) == pdPASS);
}
