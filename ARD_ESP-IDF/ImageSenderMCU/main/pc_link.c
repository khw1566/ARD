#include "pc_link.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ard_network.h"
#include "app_config.h"
#include "camera.h"

static const char *TAG = "PC_LINK";
static SemaphoreHandle_t http_lock, config_lock;
static pc_config_t config = {.resolution = APP_DEFAULT_RESOLUTION, .quality = APP_DEFAULT_QUALITY};
static pc_capture_cb_t capture_cb;
static char response[1024];
static size_t response_len;
static bool pc_reachable;
static int64_t next_failure_log;

static esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t room = sizeof(response) - 1 - response_len;
        size_t n = (size_t)event->data_len < room ? (size_t)event->data_len : room;
        memcpy(response + response_len, event->data, n);
        response_len += n;
        response[response_len] = 0;
    }
    return ESP_OK;
}

// One request at a time: the response buffer is shared.
static bool request(const char *path, const char *type, const void *body, size_t len, int timeout_ms, char *out, size_t out_size) {
    if (!ard_network_connected()) return false;
    char host[65], url[160];
    ard_network_server(host, sizeof(host));
    snprintf(url, sizeof(url), "http://%s:%d%s", host, APP_HTTP_PORT, path);
    xSemaphoreTake(http_lock, portMAX_DELAY);
    response_len = 0;
    response[0] = 0;
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = timeout_ms, .event_handler = http_event};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = false;
    if (client) {
        if (body) {
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_header(client, "Content-Type", type);
            esp_http_client_set_post_field(client, body, len);
        }
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        ok = err == ESP_OK && status >= 200 && status < 300;
        int64_t now = esp_timer_get_time();
        if (ok && !pc_reachable)
            ESP_LOGI(TAG, "PC server connected: %s:%d", host, APP_HTTP_PORT);
        if (!ok && (pc_reachable || now >= next_failure_log)) {
            ESP_LOGW(TAG, "PC %s:%d%s failed: %s, HTTP %d, Wi-Fi=%s. Check PC server and firewall.",
                     host, APP_HTTP_PORT, path, esp_err_to_name(err), status,
                     ard_network_connected() ? "connected" : "disconnected");
            next_failure_log = now + 10000000;
        }
        pc_reachable = ok;
        if (ok && out) strlcpy(out, response, out_size);
    }
    xSemaphoreGive(http_lock);
    return ok;
}

bool pc_link_log(const char *fmt, ...) {
    char message[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return false;
    cJSON_AddStringToObject(doc, "source", "ImageSenderMCU");
    cJSON_AddStringToObject(doc, "message", message);
    char *json = cJSON_PrintUnformatted(doc);
    bool ok = json && request("/api/log", "application/json", json, strlen(json), 3000, NULL, 0);
    cJSON_free(json);
    cJSON_Delete(doc);
    return ok;
}

bool pc_link_upload_jpeg(const uint8_t *jpeg, size_t len) {
    return request("/upload", "image/jpeg", jpeg, len, 10000, NULL, 0);
}

void pc_link_get_config(pc_config_t *out) {
    xSemaphoreTake(config_lock, portMAX_DELAY);
    *out = config;
    xSemaphoreGive(config_lock);
}

static void poll_config(void) {
    static char body[sizeof(response)];
    if (!request("/api/imagesendermcu/config", NULL, NULL, 0, 4000, body, sizeof(body))) return;
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
    if (cJSON_IsString(cmd) && !strcasecmp(cmd->valuestring, "capture") && capture_cb) capture_cb("poll");
    cJSON_Delete(doc);
}

static void heartbeat(void) {
    char ip[16], json[256];
    ard_network_local_ip(ip, sizeof(ip));
    snprintf(json, sizeof(json),
             "{\"source\":\"ImageSenderMCU\",\"ip\":\"%s\",\"rssi\":%d,\"board\":\"ESP32-S3\","
             "\"firmware\":\"esp-idf\",\"protocol\":2,\"camera_model\":\"%s\",\"resolution\":\"%s\"}",
             ip, ard_network_rssi(), camera_sensor_name(), camera_resolution());
    request("/api/heartbeat", "application/json", json, strlen(json), 4000, NULL, 0);
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
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void pc_link_init(void) {
    if (config_lock) return;
    http_lock = xSemaphoreCreateMutex();
    config_lock = xSemaphoreCreateMutex();
    configASSERT(http_lock && config_lock);
}

void pc_link_start(pc_capture_cb_t on_capture) {
    pc_link_init();
    capture_cb = on_capture;
    configASSERT(xTaskCreate(link_task, "pc_link", 6144, NULL, 4, NULL) == pdPASS);
}
