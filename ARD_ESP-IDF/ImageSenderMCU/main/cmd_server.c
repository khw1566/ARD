#include "cmd_server.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "app_config.h"

static const char *TAG = "CMD_TCP";
static cmd_capture_cb_t capture_cb;
static cmd_stats_cb_t stats_cb;
static cmd_reset_cb_t reset_cb;

static void reply(int client, const char *text) { send(client, text, strlen(text), 0); }

static void handle(int client) {
    struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char line[64];
    int len = 0;
    bool complete = false;
    const int64_t deadline = esp_timer_get_time() + 1000000;
    while (len < (int)sizeof(line) - 1) {
        if (esp_timer_get_time() >= deadline) break;
        int n = recv(client, line + len, sizeof(line) - 1 - len, 0);
        if (n <= 0) break;
        len += n;
        char *end = memchr(line, '\n', len);
        if (end) { len = end - line; complete = true; break; }
    }
    if (!complete) { reply(client, "ERR:INCOMPLETE\n"); return; }
    line[len] = 0;
    while (len && isspace((unsigned char)line[len - 1])) line[--len] = 0;
    if (!strncasecmp(line, "capture ", 8)) {
        char *end = NULL;
        unsigned long id = strtoul(line + 8, &end, 10);
        if (line[8] < '0' || line[8] > '9' || !end || *end || !id || id >= 0x80000000UL) {
            reply(client, "ERR:INVALID_ID\n"); return;
        }
        if (!capture_cb("tcp", (uint32_t)id)) { reply(client, "ERR:BUSY\n"); return; }
        char ack[64]; snprintf(ack, sizeof(ack), "ACK:CAPTURE_START,%lu\n", id);
        reply(client, ack);
    } else if (!strcasecmp(line, "c") || !strcasecmp(line, "capture")) {
        reply(client, capture_cb("tcp", 0) ? "ACK:CAPTURE_START\n" : "ERR:BUSY\n");
    } else if (!strcasecmp(line, "reset") || !strcasecmp(line, "reset_camera")) {
        reset_cb();
        reply(client, "ACK:RESET_PENDING\n");
    } else if (!strcasecmp(line, "ping")) {
        char stats[192] = "";
        if (stats_cb) stats_cb(stats, sizeof(stats));
        char out[224];
        snprintf(out, sizeof(out), "ACK:PONG,%s\n", stats);
        reply(client, out);
    } else if (len) {
        ESP_LOGW(TAG, "Unknown command: %s", line);
        reply(client, "ERR:UNKNOWN\n");
    }
}

static void server_task(void *arg) {
    while (true) {
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        int yes = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(APP_CMD_TCP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) || listen(listener, 2)) {
            ESP_LOGE(TAG, "bind/listen %d failed: errno %d", APP_CMD_TCP_PORT, errno);
            close(listener);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        ESP_LOGI(TAG, "Listening on TCP %d", APP_CMD_TCP_PORT);
        while (true) {
            int client = accept(listener, NULL, NULL);
            if (client < 0) { ESP_LOGW(TAG, "accept failed: errno %d", errno); break; }
            handle(client);
            shutdown(client, SHUT_RDWR);
            close(client);
        }
        close(listener);
    }
}

void cmd_server_start(cmd_capture_cb_t on_capture, cmd_reset_cb_t on_reset, cmd_stats_cb_t stats) {
    capture_cb = on_capture;
    reset_cb = on_reset;
    stats_cb = stats;
    configASSERT(xTaskCreate(server_task, "cmd_tcp", 4096, NULL, 4, NULL) == pdPASS);
}
