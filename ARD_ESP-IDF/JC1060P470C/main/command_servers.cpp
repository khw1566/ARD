// Serial console (UART0) and TCP command server (port 5002).
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "BoundedLine.h"
#include "ard_network.h"
#include "wifi_debug.h"
#include "board.h"
#include "cJSON.h"
#include "commands.h"
#include "pc_link.h"
#include "onboard_audio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "commands";

namespace {

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void sendText(int sock, const char *text) {
    size_t sent = 0, length = strlen(text);
    while (sent < length) {
        const int n = send(sock, text + sent, length - sent, 0);
        if (n <= 0) return;
        sent += n;
    }
}

void respond(int sock, const char *request) {
    char line[128];
    strlcpy(line, request, sizeof(line));
    for (char *p = line; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    if (!strcmp(line, "ping")) {
        sendText(sock, "ACK:PONG,role=HMI\n");
    } else if (!strcmp(line, "audio_status")) {
        const auto s = onboard_audio_status();
        char reply[256];
        snprintf(reply, sizeof(reply), "{\"ready\":%s,\"recording\":%s,\"playing\":%s,\"volume\":%d,\"peak\":%d,\"clips\":%lu,\"errors\":%lu}\n",
                 s.ready ? "true" : "false", s.recording ? "true" : "false", s.playing ? "true" : "false",
                 s.volume, s.peak, (unsigned long)s.uploadedClips, (unsigned long)s.uploadErrors);
        sendText(sock, reply);
    } else if (!strcmp(line, "status")) {
        const PcStatus pc = pc_status();
        cJSON *doc = cJSON_CreateObject();
        cJSON_AddStringToObject(doc, "role", "HMI");
        cJSON_AddStringToObject(doc, "board", "JC1060P470C");
        cJSON_AddBoolToObject(doc, "pc_reachable", pc.reachable);
        cJSON_AddStringToObject(doc, "drive_ip", pc.board[PC_DRIVE].ip);
        cJSON_AddBoolToObject(doc, "drive_online", pc.board[PC_DRIVE].online);
        char *json = cJSON_PrintUnformatted(doc);
        cJSON_Delete(doc);
        if (json) { sendText(sock, json); sendText(sock, "\r\n"); cJSON_free(json); }
    } else {
        sendText(sock, command_execute(line, false) ? "ACK:QUEUED\r\n" : "ERR:REJECTED\r\n");
    }
}

// One command per connection; fragmented or silent clients time out quickly.
void serveClient(int sock) {
    const int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    timeval tv = {0, 100000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    BoundedLine line;
    const uint32_t connectedAt = nowMs();
    uint32_t lastByte = connectedAt;
    char buffer[64];
    for (;;) {
        const int n = recv(sock, buffer, sizeof(buffer), 0);
        if (n == 0) return; // peer closed
        if (n > 0) {
            lastByte = nowMs();
            for (int i = 0; i < n; ++i) {
                const auto result = line.push(buffer[i]);
                if (result == BoundedLine::TooLong) { sendText(sock, "ERR:TOO_LONG\r\n"); return; }
                if (result == BoundedLine::Ready) { respond(sock, line.value()); return; }
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return;
        }
        const uint32_t now = nowMs();
        if (now - lastByte >= 500 || now - connectedAt >= 1000) {
            if (line.partial()) sendText(sock, "ERR:TIMEOUT\r\n");
            return;
        }
    }
}

void tcpTask(void *) {
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    int listener = -1;
    for (;;) {
        esp_task_wdt_reset();
        if (listener < 0) {
            listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            const int one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(ARD_TCP_COMMAND_PORT);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (listener < 0 || bind(listener, (sockaddr *)&addr, sizeof(addr)) || listen(listener, 2)) {
                ESP_LOGE(TAG, "TCP %d listen failed (errno %d); retrying", ARD_TCP_COMMAND_PORT, errno);
                if (listener >= 0) close(listener);
                listener = -1;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            ESP_LOGI(TAG, "Command server on TCP %d", ARD_TCP_COMMAND_PORT);
        }
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listener, &ready);
        timeval tv = {1, 0};
        if (select(listener + 1, &ready, nullptr, nullptr, &tv) <= 0) continue;
        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) continue;
        serveClient(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}

void consoleTask(void *) {
    BoundedLine line;
    uint8_t buffer[64];
    for (;;) {
        const int n = uart_read_bytes(UART_NUM_0, buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; ++i) {
            const auto result = line.push((char)buffer[i]);
            if (result == BoundedLine::Ready) {
                char command[128];
                strlcpy(command, line.value(), sizeof(command));
                line.reset();
                // Serial only: TCP clients on the network must not redirect the PC link.
                if (!strcmp(command, "wifi_debug")) {
                    wifi_debug_dump("console");
                } else if (!strncasecmp(command, "server=", 7)) {
                    const esp_err_t err = ard_network_set_server(command + 7);
                    if (err == ESP_OK) printf("OK: PC server %s\n", command + 7);
                    else printf("ERR:SERVER %s\n", esp_err_to_name(err));
                } else if (!command_execute(command)) printf("ERR:REJECTED\n");
            } else if (result == BoundedLine::TooLong) {
                printf("ERR:TOO_LONG\n");
            }
        }
    }
}

} // namespace

esp_err_t command_servers_start() {
    esp_err_t err = uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0);
    if (err != ESP_OK) return err;
    if (xTaskCreatePinnedToCore(consoleTask, "console", 4096, nullptr, 9, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(tcpTask, "tcp_cmd", 5120, nullptr, 10, nullptr, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
