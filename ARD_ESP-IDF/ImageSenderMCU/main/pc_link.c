#include "pc_link.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ard_network.h"
#include "app_config.h"
#include "camera.h"

static const char *TAG = "PC_LINK";
static SemaphoreHandle_t config_lock;
static QueueHandle_t logs;
static pc_config_t config = {.resolution = APP_DEFAULT_RESOLUTION, .quality = APP_DEFAULT_QUALITY};
static pc_capture_cb_t capture_cb;
static pc_reset_cb_t reset_cb;
typedef struct { char *data; size_t size, used; } response_t;

static esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        response_t *response = event->user_data;
        if (!response || !response->data || !response->size) return ESP_OK;
        size_t room = response->size - 1 - response->used;
        size_t n = (size_t)event->data_len < room ? (size_t)event->data_len : room;
        memcpy(response->data + response->used, event->data, n);
        response->used += n;
        response->data[response->used] = 0;
    }
    return ESP_OK;
}

// Per-request client and response: a slow log/heartbeat cannot block an upload.
static bool request_id(const char *path, const char *type, const void *body, size_t len, int timeout_ms, char *out, size_t out_size, uint32_t capture_id) {
    if (!ard_network_connected()) return false;
    char host[65], url[160];
    ard_network_server(host, sizeof(host));
    snprintf(url, sizeof(url), "http://%s:%d%s", host, APP_HTTP_PORT, path);
    response_t response = {.data = out, .size = out_size};
    if (out && out_size) out[0] = 0;
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = timeout_ms, .event_handler = http_event, .user_data = &response};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = false;
    if (client) {
        if (body) {
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_header(client, "Content-Type", type);
            esp_http_client_set_post_field(client, body, len);
        }
        if (capture_id) {
            char id[16]; snprintf(id, sizeof(id), "%lu", (unsigned long)capture_id);
            esp_http_client_set_header(client, "X-ARD-Capture-ID", id);
        }
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        ok = err == ESP_OK && status >= 200 && status < 300;
        if (!ok) {
            ESP_LOGW(TAG, "PC %s:%d%s failed: %s, HTTP %d, Wi-Fi=%s. Check PC server and firewall.",
                     host, APP_HTTP_PORT, path, esp_err_to_name(err), status,
                     ard_network_connected() ? "connected" : "disconnected");
        }
    }
    return ok;
}

static bool request(const char *path, const char *type, const void *body, size_t len, int timeout_ms, char *out, size_t out_size) {
    return request_id(path, type, body, len, timeout_ms, out, out_size, 0);
}

bool pc_link_log(const char *fmt, ...) {
    char message[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    return logs && xQueueOverwrite(logs, message) == pdTRUE;
}

static void send_log(const char *message) {
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return;
    cJSON_AddStringToObject(doc, "source", "ImageSenderMCU");
    cJSON_AddStringToObject(doc, "message", message);
    char *json = cJSON_PrintUnformatted(doc);
    if (json) request("/api/log", "application/json", json, strlen(json), 500, NULL, 0);
    cJSON_free(json);
    cJSON_Delete(doc);
}

bool pc_link_upload_jpeg(const uint8_t *jpeg, size_t len) {
    return request("/upload", "image/jpeg", jpeg, len, 5000, NULL, 0);
}

bool pc_link_upload_capture(const uint8_t *jpeg, size_t len, uint32_t capture_id) {
    return request_id("/upload", "image/jpeg", jpeg, len, 5000, NULL, 0, capture_id);
}

void pc_link_get_config(pc_config_t *out) {
    xSemaphoreTake(config_lock, portMAX_DELAY);
    *out = config;
    xSemaphoreGive(config_lock);
}

static void poll_config(void) {
    char body[1024];
    if (!request("/api/imagesendermcu/config", NULL, NULL, 0, 1000, body, sizeof(body))) return;
    cJSON *doc = cJSON_Parse(body);
    if (!doc) { ESP_LOGW(TAG, "Invalid config JSON"); return; }
    pc_config_t next;
    pc_link_get_config(&next);
    const cJSON *res = cJSON_GetObjectItemCaseSensitive(doc, "resolution");
    if (cJSON_IsString(res) && strlen(res->valuestring) < sizeof(next.resolution))
        strlcpy(next.resolution, res->valuestring, sizeof(next.resolution));
    const cJSON *quality = cJSON_GetObjectItemCaseSensitive(doc, "jpeg_quality");
    // PC sends 0..63 as the esp32-camera scale (lower = better).
    if (cJSON_IsNumber(quality) && quality->valueint >= 0 && quality->valueint <= 63) next.quality = quality->valueint;
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(doc, "transmission_mode");
    if (cJSON_IsString(mode)) {
        if (!strcasecmp(mode->valuestring, "TCP")) next.tcp_mode = true;
        else if (!strcasecmp(mode->valuestring, "UDP")) next.tcp_mode = false;
    }
    xSemaphoreTake(config_lock, portMAX_DELAY);
    bool changed = memcmp(&next, &config, sizeof(next)) != 0;
    config = next;
    xSemaphoreGive(config_lock);
    if (changed) ESP_LOGI(TAG, "PC config: %s Q%d %s", next.resolution, next.quality, next.tcp_mode ? "TCP" : "UDP");
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(doc, "command");
    if (cJSON_IsString(cmd) && capture_cb) {
        if (!strcasecmp(cmd->valuestring, "capture")) capture_cb("poll", 0);
        else if (!strncasecmp(cmd->valuestring, "capture ", 8)) {
            char *end = NULL;
            unsigned long id = strtoul(cmd->valuestring + 8, &end, 10);
            if (end && !*end && id && id < 0x80000000UL) capture_cb("poll", (uint32_t)id);
        } else if (!strcasecmp(cmd->valuestring, "reset_camera") && reset_cb) reset_cb();
    }
    cJSON_Delete(doc);
}

static void heartbeat(void) {
    char ip[16], json[256];
    ard_network_local_ip(ip, sizeof(ip));
    snprintf(json, sizeof(json),
             "{\"source\":\"ImageSenderMCU\",\"ip\":\"%s\",\"rssi\":%d,\"board\":\"ESP32-S3\","
             "\"firmware\":\"esp-idf\",\"protocol\":2,\"camera_model\":\"%s\",\"resolution\":\"%s\"}",
             ip, ard_network_rssi(), camera_sensor_name(), camera_resolution());
    request("/api/heartbeat", "application/json", json, strlen(json), 1000, NULL, 0);
}

static void link_task(void *arg) {
    int64_t next_config = 0, next_heartbeat = 0;
    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        if (ard_network_connected()) {
            if (now >= next_heartbeat) {
                heartbeat();
                next_heartbeat = now + APP_HEARTBEAT_INTERVAL_MS;
            }
            if (now >= next_config) {
                poll_config();
                next_config = esp_timer_get_time() / 1000 + APP_CONFIG_INTERVAL_MS;
            }
            char message[200];
            if (xQueueReceive(logs, message, 0) == pdTRUE) send_log(message);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void pc_link_init(void) {
    if (config_lock) return;
    config_lock = xSemaphoreCreateMutex();
    logs = xQueueCreate(1, 200);
    configASSERT(logs && config_lock);
}

void pc_link_start(pc_capture_cb_t on_capture, pc_reset_cb_t on_reset) {
    pc_link_init();
    capture_cb = on_capture;
    reset_cb = on_reset;
    configASSERT(xTaskCreate(link_task, "pc_link", 6144, NULL, 4, NULL) == pdPASS);
}
