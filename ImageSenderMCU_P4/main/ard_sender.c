#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "cam_ov5647.h"
#include "ard_config.h"
#include "ard_sender.h"
#include "ard_network.h"

static const char *TAG = "ARD_P4";
static EventGroupHandle_t network;
static char local_ip[16];
static portMUX_TYPE ip_lock = portMUX_INITIALIZER_UNLOCKED;
static bool tcp_mode;
static bool capture_pending;
static char response[2048];
static size_t response_len;

void ard_sender_network(bool ready, const char *ip) {
    if (ready) {
        taskENTER_CRITICAL(&ip_lock);
        snprintf(local_ip, sizeof(local_ip), "%s", ip);
        taskEXIT_CRITICAL(&ip_lock);
        xEventGroupSetBits(network, 1);
    } else xEventGroupClearBits(network, 1);
}

static esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (event->data_len > sizeof(response) - 1 - response_len) return ESP_ERR_NO_MEM;
        memcpy(response + response_len, event->data, event->data_len);
        response_len += event->data_len;
        response[response_len] = 0;
    }
    return ESP_OK;
}

static bool request(const char *path, const char *type, const void *body, size_t len) {
    char url[192];
    char host[65];
    ard_network_server(host, sizeof(host));
    snprintf(url, sizeof(url), "http://%s:%d%s", host, ARD_HTTP_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 4000, .event_handler = http_event,
    };
    response_len = 0;
    response[0] = 0;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    if (body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", type);
        esp_http_client_set_post_field(client, body, len);
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    bool ok = err == ESP_OK && status >= 200 && status < 300;
    if (!ok) ESP_LOGW(TAG, "%s failed: %s HTTP %d", path, esp_err_to_name(err), status);
    return ok;
}

static void log_pc(const char *message) {
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return;
    cJSON_AddStringToObject(doc, "source", "ImageSenderMCU");
    cJSON_AddStringToObject(doc, "message", message);
    char *json = cJSON_PrintUnformatted(doc);
    if (json) { request("/api/log", "application/json", json, strlen(json)); cJSON_free(json); }
    cJSON_Delete(doc);
}

static bool send_udp(const uint8_t *jpeg, size_t len) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return false;
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in dest = {.sin_family = AF_INET, .sin_port = htons(ARD_UDP_PORT)};
    char host[65];
    ard_network_server(host, sizeof(host));
    if (inet_pton(AF_INET, host, &dest.sin_addr) != 1) { close(sock); return false; }
    uint8_t packet[1437];
    bool ok = true;
    for (size_t offset = 0; offset < len;) {
        size_t count = len - offset;
        if (count > 1436) count = 1436;
        packet[0] = offset == 0 ? 0 : (offset + count == len ? 2 : 1);
        memcpy(packet + 1, jpeg + offset, count);
        bool sent = false;
        for (int retry = 0; retry < 5; ++retry) {
            if (sendto(sock, packet, count + 1, 0, (struct sockaddr *)&dest, sizeof(dest)) == count + 1) { sent = true; break; }
            vTaskDelay(pdMS_TO_TICKS(10 * (retry + 1)));
        }
        if (!sent) { ok = false; break; }
        offset += count;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    // Receiver requires an END even when the entire JPEG fit in START.
    if (ok && len <= 1436) {
        packet[0] = 2;
        ok = sendto(sock, packet, 1, 0, (struct sockaddr *)&dest, sizeof(dest)) == 1;
    }
    close(sock);
    return ok;
}

static void capture(void) {
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = cam_ov5647_get_frame(&jpeg, &len, 2000);
    if (err != ESP_OK) { log_pc("P4 capture failed: no fresh JPEG; check camera serial logs"); return; }
    int64_t start = esp_timer_get_time();
    bool ok = tcp_mode ? request("/upload", "image/jpeg", jpeg, len) : send_udp(jpeg, len);
    cam_ov5647_return_frame(jpeg);
    char message[160];
    snprintf(message, sizeof(message), "P4 %s %s: %zu bytes, %lld ms, OV5647 800x640", tcp_mode ? "TCP" : "UDP", ok ? "sent" : "failed", len, (long long)((esp_timer_get_time()-start)/1000));
    ESP_LOGI(TAG, "%s", message);
    log_pc(message);
}

static void poll_config(void) {
    if (!request("/api/imagesendermcu/config", NULL, NULL, 0)) return;
    cJSON *doc = cJSON_Parse(response);
    if (!doc) { ESP_LOGW(TAG, "Invalid config JSON"); return; }
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(doc, "transmission_mode");
    if (cJSON_IsString(mode)) {
        if (!strcasecmp(mode->valuestring, "TCP")) tcp_mode = true;
        else if (!strcasecmp(mode->valuestring, "UDP")) tcp_mode = false;
    }
    const cJSON *quality = cJSON_GetObjectItemCaseSensitive(doc, "jpeg_quality");
    if (cJSON_IsNumber(quality) && quality->valuedouble >= 0 && quality->valuedouble <= 50)
        cam_ov5647_set_quality(100 - quality->valueint * 80 / 50);
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(doc, "command");
    if (cJSON_IsString(cmd) && !strcasecmp(cmd->valuestring, "capture")) capture_pending = true;
    cJSON_Delete(doc);
}

static void sender_task(void *arg) {
    int64_t next_config = 0, next_heartbeat = 0;
    while (true) {
        xEventGroupWaitBits(network, 1, pdFALSE, pdTRUE, portMAX_DELAY);
        int64_t now = esp_timer_get_time()/1000;
        if (now >= next_heartbeat) {
            char json[256];
            char ip[16];
            taskENTER_CRITICAL(&ip_lock);
            strlcpy(ip, local_ip, sizeof(ip));
            taskEXIT_CRITICAL(&ip_lock);
            snprintf(json, sizeof(json), "{\"source\":\"ImageSenderMCU\",\"ip\":\"%s\",\"rssi\":0,\"board\":\"ESP32-P4\",\"camera_model\":\"OV5647\",\"resolution\":\"800x640\"}", ip);
            request("/api/heartbeat", "application/json", json, strlen(json));
            next_heartbeat = esp_timer_get_time()/1000 + ARD_HEARTBEAT_INTERVAL_MS;
        }
        if (now >= next_config) {
            poll_config();
            next_config = esp_timer_get_time()/1000 + ARD_CONFIG_INTERVAL_MS;
        }
        if (capture_pending) { capture_pending = false; capture(); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void ard_sender_start(void) {
    network = xEventGroupCreate();
    configASSERT(network);
    configASSERT(xTaskCreate(sender_task, "ard_sender", 8192, NULL, 4, NULL) == pdPASS);
}
