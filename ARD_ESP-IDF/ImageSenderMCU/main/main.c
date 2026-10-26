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
static portMUX_TYPE capture_lock = portMUX_INITIALIZER_UNLOCKED;
static bool capture_busy, capture_pending, reset_pending, camera_ready;
static uint32_t pending_id, accepted_id, capture_epoch, camera_resets;
static int64_t busy_since;

// One accepted capture at a time. Duplicate IDs only replay the ACK; a different
// request while busy is rejected so it cannot silently become a delayed extra shot.
static bool request_capture(const char *source, uint32_t id) {
    taskENTER_CRITICAL(&capture_lock);
    if (id && id == accepted_id && !reset_pending) {
        taskEXIT_CRITICAL(&capture_lock);
        return true; // replay after lost ACK: never capture the same request twice
    }
    if (!capture_task_handle || capture_busy || reset_pending) {
        taskEXIT_CRITICAL(&capture_lock);
        return false;
    }
    capture_busy = capture_pending = true;
    pending_id = accepted_id = id;
    busy_since = esp_timer_get_time();
    taskEXIT_CRITICAL(&capture_lock);
    ESP_LOGI(TAG, "Capture accepted (%s, id=%lu)", source, (unsigned long)id);
    xTaskNotifyGive(capture_task_handle);
    return true;
}

static void request_reset(void) {
    taskENTER_CRITICAL(&capture_lock);
    if (!capture_busy && !reset_pending) busy_since = esp_timer_get_time();
    reset_pending = true;
    capture_pending = false;
    ++capture_epoch; // old capture may return, but its frame must not be sent
    taskEXIT_CRITICAL(&capture_lock);
    if (capture_task_handle) xTaskNotifyGive(capture_task_handle);
}

static bool epoch_current(uint32_t epoch) {
    taskENTER_CRITICAL(&capture_lock);
    bool current = epoch == capture_epoch && !reset_pending;
    taskEXIT_CRITICAL(&capture_lock);
    return current;
}

static void stats_text(char *out, int size) {
    taskENTER_CRITICAL(&capture_lock);
    const bool busy = capture_busy, reset = reset_pending, ready = camera_ready;
    const uint32_t id = accepted_id, resets = camera_resets;
    taskEXIT_CRITICAL(&capture_lock);
    snprintf(out, size, "v=3,busy=%d,reset=%d,ready=%d,id=%lu,resets=%lu,cap_ms=%lu,tx_ms=%lu,ok=%lu,fail=%lu",
             busy, reset, ready, (unsigned long)id, (unsigned long)resets, (unsigned long)last_capture_ms,
             (unsigned long)last_tx_ms, (unsigned long)frames_ok, (unsigned long)frames_failed);
}

static void capture_and_send(const pc_config_t *cfg, uint32_t id, uint32_t epoch) {
    if (!ard_network_connected()) {
        ESP_LOGW(TAG, "Capture skipped: Wi-Fi not connected");
        return;
    }
    int64_t t0 = esp_timer_get_time();
    camera_fb_t *fb = camera_capture_fresh();
    if (!fb && epoch_current(epoch)) {
        ESP_LOGW(TAG, "Frame timeout: reinitialising camera once");
        taskENTER_CRITICAL(&capture_lock);
        camera_ready = false;
        ++camera_resets;
        taskEXIT_CRITICAL(&capture_lock);
        esp_err_t reset_err = camera_reset(cfg->resolution, cfg->quality);
        taskENTER_CRITICAL(&capture_lock);
        camera_ready = reset_err == ESP_OK;
        taskEXIT_CRITICAL(&capture_lock);
        if (reset_err == ESP_OK && epoch_current(epoch)) fb = camera_capture_fresh();
    }
    if (!fb) {
        taskENTER_CRITICAL(&capture_lock);
        camera_ready = false;
        taskEXIT_CRITICAL(&capture_lock);
        frames_failed++;
        ESP_LOGE(TAG, "Capture failed: no frame from camera");
        pc_link_log("Capture failed: no frame from camera (sensor %s)", camera_sensor_name());
        return;
    }
    if (!epoch_current(epoch)) { esp_camera_fb_return(fb); return; }
    last_capture_ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "Captured %u bytes %ux%u in %lu ms, mode %s", (unsigned)fb->len, fb->width, fb->height,
             (unsigned long)last_capture_ms, cfg->tcp_mode ? "TCP" : "UDP");
    char host[65];
    ard_network_server(host, sizeof(host));
    int64_t t1 = esp_timer_get_time();
    bool ok;
    char detail[160];
    if (cfg->tcp_mode || id) {
        // Tagged single shots use HTTP so the capture ID reaches the PC intact.
        // Legacy untagged streaming retains the selected UDP/TCP transport.
        ok = id ? pc_link_upload_capture(fb->buf, fb->len, id) : pc_link_upload_jpeg(fb->buf, fb->len);
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
    esp_err_t err = camera_start(cfg.resolution, cfg.quality);
    taskENTER_CRITICAL(&capture_lock);
    camera_ready = err == ESP_OK;
    taskEXIT_CRITICAL(&capture_lock);
    while (true) {
        // Wake on a capture request, or every 500 ms to apply PC config changes.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        pc_link_get_config(&cfg);
        taskENTER_CRITICAL(&capture_lock);
        const bool reset = reset_pending;
        const bool requested = capture_pending && !reset;
        const uint32_t id = pending_id, epoch = capture_epoch;
        capture_pending = false;
        taskEXIT_CRITICAL(&capture_lock);
        err = reset ? camera_reset(cfg.resolution, cfg.quality) : camera_apply(cfg.resolution, cfg.quality);
        taskENTER_CRITICAL(&capture_lock);
        camera_ready = err == ESP_OK;
        if (reset && epoch == capture_epoch) {
            reset_pending = false;
            // Keep the last ID deduplicated: a delayed pre-reset poll must not
            // restart a capture the operator has already cancelled.
            ++camera_resets;
        }
        taskEXIT_CRITICAL(&capture_lock);
        if (requested && err == ESP_OK && epoch_current(epoch)) capture_and_send(&cfg, id, epoch);
        else if (requested) { ++frames_failed; pc_link_log("Capture skipped: camera not ready"); }
        taskENTER_CRITICAL(&capture_lock);
        capture_busy = capture_pending;
        taskEXIT_CRITICAL(&capture_lock);
        if (err != ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void capture_watchdog(void *arg) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        taskENTER_CRITICAL(&capture_lock);
        const bool stuck = (capture_busy || reset_pending) && esp_timer_get_time() - busy_since > 30000000;
        taskEXIT_CRITICAL(&capture_lock);
        if (stuck) {
            // Only an accepted user capture/reset can arm this guard. No idle reboot loop.
            ESP_LOGE(TAG, "Capture/reset stalled >30s; restarting MCU");
            esp_restart();
        }
    }
}

static void console_task(void *arg) {
    // UART0 is the USB-UART port on the N16R8 board (same as the Arduino build).
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "Serial commands: c=capture, r=camera reset, w=network setup, s=stats");
    uint8_t ch;
    while (true) {
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == 'c') request_capture("serial", 0);
        else if (ch == 'r') request_reset();
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
    pc_link_start(request_capture, request_reset);
    cmd_server_start(request_capture, request_reset, stats_text);
    configASSERT(xTaskCreate(capture_watchdog, "capture_guard", 2048, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(console_task, "console", 3072, NULL, 2, NULL) == pdPASS);
}
