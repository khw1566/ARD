#include "pc_link.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ard_network.h"
#include "audio.h"
#include "board.h"
#include "driver/temperature_sensor.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inputs.h"

#define JOG_INTERVAL_MS  120 // server moves the robot 50 steps per update while held
#define IDLE_INTERVAL_MS 2000
#define HEARTBEAT_MS     2000
#define HTTP_TIMEOUT_MS  1000

#define BIT_STATE 1u
#define BIT_STOP  2u

static const char *TAG = "PC_LINK";
static TaskHandle_t link_task_handle;
static temperature_sensor_handle_t tsens;
static esp_http_client_handle_t client;

float pc_link_temperature(void) {
    float c;
    return tsens && temperature_sensor_get_celsius(tsens, &c) == ESP_OK ? c : NAN;
}

void pc_link_notify_state(void) {
    if (link_task_handle) xTaskNotify(link_task_handle, BIT_STATE, eSetBits);
}

void pc_link_request_stop(void) {
    if (link_task_handle) xTaskNotify(link_task_handle, BIT_STOP, eSetBits);
}

// One client is kept between requests so a keep-alive server skips the TCP
// handshake; the Arduino build opened a new connection for every POST, twice
// per 120 ms while the joystick was held.
static bool post(const char *path, const char *json) {
    if (!ard_network_connected()) return false;
    char host[65], url[160];
    ard_network_server(host, sizeof(host));
    snprintf(url, sizeof(url), "http://%s:%d%s", host, PC_HTTP_PORT, path);
    if (!client) {
        esp_http_client_config_t cfg = {.url = url, .timeout_ms = HTTP_TIMEOUT_MS, .method = HTTP_METHOD_POST};
        client = esp_http_client_init(&cfg);
        if (!client) return false;
    } else {
        esp_http_client_set_url(client, url);
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    int code = esp_http_client_get_status_code(client);
    bool ok = err == ESP_OK && code == 200;
    if (!ok) {
        ESP_LOGW(TAG, "%s -> %s failed: %s, HTTP %d", path, host, esp_err_to_name(err), code);
        // Start clean next time (server restarted, IP changed, half-open socket).
        esp_http_client_cleanup(client);
        client = NULL;
    }
    return ok;
}

static void fmt_temp(char *out, size_t size) {
    float t = pc_link_temperature();
    if (isnan(t)) strlcpy(out, "0", size);
    else snprintf(out, size, "%.1f", t);
}

static void send_state(const control_state_t *s) {
    char json[256], temp[12];
    fmt_temp(temp, sizeof(temp));
    snprintf(json, sizeof(json),
             "{\"mode\":\"%c\",\"sub_mode\":%d,\"rssi\":%d,\"temp\":%s,"
             "\"joystick\":{\"up\":%s,\"down\":%s,\"left\":%s,\"right\":%s}}",
             s->mode, s->sub_mode, ard_network_rssi(), temp, s->joy[JOY_UP] ? "true" : "false",
             s->joy[JOY_DOWN] ? "true" : "false", s->joy[JOY_LEFT] ? "true" : "false",
             s->joy[JOY_RIGHT] ? "true" : "false");
    post("/api/control/update", json);
}

static void send_heartbeat(const control_state_t *s) {
    char json[384], ip[16], temp[12], ssid[33] = "";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) strlcpy(ssid, (const char *)ap.ssid, sizeof(ssid));
    // SSIDs may contain quotes or backslashes; drop them rather than break the JSON.
    for (char *c = ssid; *c; ++c)
        if (*c == '"' || *c == '\\' || (unsigned char)*c < 0x20) *c = '_';
    ard_network_local_ip(ip, sizeof(ip));
    fmt_temp(temp, sizeof(temp));
    audio_status_t a;
    audio_get_status(&a);
    // No "status" key: the PC owns the voice state (Recording/Processing/...).
    snprintf(json, sizeof(json),
             "{\"source\":\"ControlMCU\",\"board\":\"ESP32-S3\",\"firmware\":\"esp-idf\",\"ip\":\"%s\","
             "\"rssi\":%d,\"temp\":%s,\"mode\":\"%c\",\"sub_mode\":%d,\"ssid\":\"%s\",\"volume\":%d,"
             "\"muted\":%s,\"audio_packets\":%lu,\"audio_underruns\":%lu}",
             ip, ard_network_rssi(), temp, s->mode, s->sub_mode, ssid, a.volume, a.muted ? "true" : "false",
             (unsigned long)a.packets, (unsigned long)a.underruns);
    static uint32_t ok_count;
    if (post("/api/heartbeat", json) && ok_count++ % 30 == 0)
        ESP_LOGI(TAG, "Heartbeat #%lu OK (IP %s, RSSI %d)", (unsigned long)ok_count, ip, ard_network_rssi());
}

static void link_task(void *arg) {
    int64_t next_state = 0, next_heartbeat = 0;
    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t next = next_state < next_heartbeat ? next_state : next_heartbeat;
        uint32_t bits = 0;
        if (next > now) xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(next - now));
        if (!ard_network_connected()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (bits & BIT_STOP) {
            if (post("/api/controlmcu/voice/stop", "{}")) ESP_LOGI(TAG, "Stop request sent to PC");
        }
        control_state_t s;
        inputs_get(&s);
        now = esp_timer_get_time() / 1000;
        if ((bits & BIT_STATE) || now >= next_state) {
            send_state(&s);
            next_state = esp_timer_get_time() / 1000 + (inputs_any_joy(&s) ? JOG_INTERVAL_MS : IDLE_INTERVAL_MS);
        }
        if (now >= next_heartbeat) {
            send_heartbeat(&s);
            next_heartbeat = esp_timer_get_time() / 1000 + HEARTBEAT_MS;
        }
    }
}

void pc_link_start(void) {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&cfg, &tsens) != ESP_OK || temperature_sensor_enable(tsens) != ESP_OK) {
        ESP_LOGW(TAG, "Temperature sensor unavailable");
        tsens = NULL;
    }
    configASSERT(xTaskCreatePinnedToCore(link_task, "pc_link", 6144, NULL, 4, &link_task_handle, 0) == pdPASS);
}
