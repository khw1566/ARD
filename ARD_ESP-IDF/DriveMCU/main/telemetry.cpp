#include "telemetry.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include "board.h"
#include "buzzer.h"
#include "cJSON.h"
#include "gps.h"
#include "motion.h"
#include "net/ard_network.h"
#include "sensors.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "telemetry";

namespace {

struct LogMessage { char text[160]; };

QueueHandle_t logQueue;
std::atomic<uint32_t> droppedLogs{0};
std::atomic<uint32_t> heartbeatInterval{1000};
std::atomic<bool> gpsSyncRequested{false};

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void url(char *out, size_t size, const char *path) {
    char host[65];
    ard_network_server(host, sizeof(host));
    snprintf(out, size, "http://%s:%d%s", host, ARD_PC_HTTP_PORT, path);
}

// Returns the HTTP status, or a negative esp_err_t on transport failure.
int request(const char *path, esp_http_client_method_t method, const char *body, int timeoutMs,
            char *response = nullptr, size_t responseSize = 0) {
    if (!ard_network_connected()) return -1;
    char target[112];
    url(target, sizeof(target), path);
    esp_http_client_config_t config = {};
    config.url = target;
    config.method = method;
    config.timeout_ms = timeoutMs;
    config.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -ESP_ERR_NO_MEM;
    const int length = body ? (int)strlen(body) : 0;
    if (body) esp_http_client_set_header(client, "Content-Type", "application/json");
    int status;
    esp_err_t err = esp_http_client_open(client, length);
    if (err == ESP_OK && length && esp_http_client_write(client, body, length) != length) err = ESP_FAIL;
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) err = ESP_FAIL;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        if (response && responseSize) {
            const int read = esp_http_client_read_response(client, response, responseSize - 1);
            response[read > 0 ? read : 0] = 0;
        }
    } else {
        status = -err;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

void sendLog(const char *message) {
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "source", "DriveMCU");
    cJSON_AddStringToObject(doc, "message", message);
    char *payload = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!payload) return;
    const int status = request("/api/log", HTTP_METHOD_POST, payload, 500);
    if (status != 200 && ard_network_connected()) ESP_LOGW(TAG, "Log failed: %d", status);
    cJSON_free(payload);
}

void logTask(void *) {
    LogMessage message;
    for (;;) {
        if (xQueueReceive(logQueue, &message, portMAX_DELAY) == pdTRUE) {
            printf("[LOG] %s\n", message.text);
            sendLog(message.text);
        }
    }
}

void sendHeartbeat() {
    MotionStatus motion = {};
    const bool haveMotion = motion_status(&motion, 50);
    const EnvironmentSample env = sensors_environment();
    const GpsSnapshot gps = gps_snapshot();
    char ip[16];
    ard_network_ip(ip, sizeof(ip));

    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "source", "DriveMCU");
    cJSON_AddNumberToObject(doc, "rssi", ard_network_rssi());
    cJSON_AddStringToObject(doc, "ip", ip);
    // ESP32 (classic) has no on-chip temperature sensor driver in ESP-IDF 5; "temp" is omitted.
    cJSON_AddNumberToObject(doc, "tof_dist", sensors_tof_last());
    cJSON_AddStringToObject(doc, "tof_status", sensors_tof_status());
    if (haveMotion) {
        cJSON_AddNumberToObject(doc, "last_move_ms", motion.lastMoveMs);
        cJSON_AddNumberToObject(doc, "last_homing_ms", motion.lastHomingMs);
    }
    if (env.ahtValid) {
        cJSON_AddNumberToObject(doc, "env_temp", env.temperature);
        cJSON_AddNumberToObject(doc, "env_humid", env.humidity);
    }
    if (env.bmpValid) cJSON_AddNumberToObject(doc, "env_press", env.pressure);
    cJSON_AddNumberToObject(doc, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(doc, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(doc, "dropped_logs", droppedLogs.load());
    cJSON_AddNumberToObject(doc, "gps_lat", gps.enabled && gps.fix ? gps.lat : 0.0);
    cJSON_AddNumberToObject(doc, "gps_lng", gps.enabled && gps.fix ? gps.lng : 0.0);
    char *payload = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!payload) return;
    const int status = request("/api/heartbeat", HTTP_METHOD_POST, payload, 1000);
    cJSON_free(payload);
    if (status == 200) buzzer_play(MELODY_HEARTBEAT);
    else ESP_LOGW(TAG, "Heartbeat failed: %d", status);
}

// UBX-AID-INI with time only, from the PC's GPS week / time-of-week.
void syncGpsTime() {
    char body[200];
    const int status = request("/api/time", HTTP_METHOD_GET, nullptr, 500, body, sizeof(body));
    if (status != 200) { log_async("A-GPS Sync Failed: HTTP %d", status); return; }
    cJSON *doc = cJSON_Parse(body);
    const cJSON *weekItem = cJSON_GetObjectItem(doc, "week");
    const cJSON *towItem = cJSON_GetObjectItem(doc, "tow_ms");
    if (!cJSON_IsNumber(weekItem) || !cJSON_IsNumber(towItem)) {
        cJSON_Delete(doc);
        log_async("A-GPS Sync Failed: bad /api/time response");
        return;
    }
    const uint16_t week = (uint16_t)weekItem->valuedouble;
    const uint32_t tow = (uint32_t)towItem->valuedouble;
    cJSON_Delete(doc);
    uint8_t msg[56] = {0xB5, 0x62, 0x0B, 0x01, 48, 0};
    msg[22] = 0x02; // flags: time valid
    msg[24] = week & 0xFF; msg[25] = week >> 8;
    for (int i = 0; i < 4; ++i) msg[26 + i] = (tow >> (8 * i)) & 0xFF;
    msg[34] = 0xE8; msg[35] = 0x03; // tAcc 1000 ms
    uint8_t a = 0, b = 0;
    for (int i = 2; i < 54; ++i) { a += msg[i]; b += a; }
    msg[54] = a; msg[55] = b;
    gps_send(msg, sizeof(msg));
    ESP_LOGI(TAG, "A-GPS week %u tow %lu sent", week, (unsigned long)tow);
    log_async("A-GPS (Time) Synced from PC");
}

void heartbeatTask(void *) {
    uint32_t last = 0;
    for (;;) {
        if (gpsSyncRequested.exchange(false)) syncGpsTime();
        if (nowMs() - last >= heartbeatInterval) {
            last = nowMs();
            if (ard_network_connected()) sendHeartbeat();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

} // namespace

esp_err_t telemetry_start() {
    logQueue = xQueueCreate(20, sizeof(LogMessage));
    if (!logQueue) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(logTask, "log", 4096, nullptr, 4, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(heartbeatTask, "heartbeat", 5120, nullptr, 5, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void log_async(const char *format, ...) {
    if (!logQueue) return;
    LogMessage message;
    va_list args;
    va_start(args, format);
    vsnprintf(message.text, sizeof(message.text), format, args);
    va_end(args);
    if (xQueueSend(logQueue, &message, 0) != pdPASS) ++droppedLogs;
}

void telemetry_set_heartbeat_interval(uint32_t ms) { heartbeatInterval = ms; }
void telemetry_request_gps_sync() { gpsSyncRequested = true; }
uint32_t telemetry_dropped_logs() { return droppedLogs; }
