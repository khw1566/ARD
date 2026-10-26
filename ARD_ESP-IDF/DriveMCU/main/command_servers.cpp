// Serial console (UART0) and TCP command server (port 5002).
#include <stdio.h>
#include <string.h>
#include "BoundedLine.h"
#include "board.h"
#include "cJSON.h"
#include "commands.h"
#include "motion.h"
#include "sensors.h"
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
        MotionStatus s = {};
        motion_status(&s, 100);
        char reply[80];
        snprintf(reply, sizeof(reply), "ACK:PONG,move_ms=%lu,homing_ms=%lu\n",
                 (unsigned long)s.lastMoveMs, (unsigned long)s.lastHomingMs);
        sendText(sock, reply);
    } else if (!strcmp(line, "status")) {
        MotionStatus s;
        if (!motion_status(&s, 100)) { sendText(sock, "ERR:BUSY\n"); return; }
        cJSON *doc = cJSON_CreateObject();
        cJSON_AddNumberToObject(doc, "protocol", 1);
        cJSON_AddNumberToObject(doc, "boot_id", motion_boot_id());
        cJSON_AddBoolToObject(doc, "busy", s.busy);
        cJSON_AddBoolToObject(doc, "stopped", s.stopped);
        cJSON_AddBoolToObject(doc, "pid", s.pid);
        cJSON_AddNumberToObject(doc, "pid_p", s.pidP);
        cJSON_AddNumberToObject(doc, "pid_i", s.pidI);
        cJSON_AddNumberToObject(doc, "pid_d", s.pidD);
        cJSON_AddBoolToObject(doc, "jogging", s.jogging); // also tells the PC that `j` is supported
        cJSON_AddBoolToObject(doc, "program", s.program); // queued move/homing: jog rejected
        cJSON_AddNumberToObject(doc, "x", s.x);
        cJSON_AddNumberToObject(doc, "y", s.y);
        cJSON_AddNumberToObject(doc, "z", s.z);
        cJSON_AddNumberToObject(doc, "steps_per_mm", s.stepsPerMmZ);
        // Rail PID state for the PC sorting sequence and rail tools.
        float tof;
        uint32_t tofAge;
        const bool tofOk = sensors_tof(&tof, &tofAge);
        cJSON_AddNumberToObject(doc, "tof_mm", tofOk ? tof : 0);
        cJSON_AddBoolToObject(doc, "tof_ok", tofOk);
        cJSON_AddNumberToObject(doc, "tof_age_ms", tofAge);
        cJSON_AddStringToObject(doc, "tof_status", sensors_tof_status());
        cJSON_AddNumberToObject(doc, "rail_target", s.targetRailDist);
        cJSON_AddNumberToObject(doc, "rail_dir", s.railDir);
        cJSON_AddBoolToObject(doc, "rail_settled", s.railSettled && tofOk);
        // Z safety limits (ToF mm) and the last safety stop/refusal.
        cJSON_AddBoolToObject(doc, "rail_limit_on", s.railLimitOn);
        cJSON_AddNumberToObject(doc, "rail_min", s.railMinMm);
        cJSON_AddNumberToObject(doc, "rail_max", s.railMaxMm);
        cJSON_AddNumberToObject(doc, "rail_limit_count", s.railLimitCount);
        cJSON_AddStringToObject(doc, "rail_limit", s.railLimitReason);
        // Disc tilt safety limit (rotation is not limited) and the last refusal/halt.
        cJSON_AddNumberToObject(doc, "tilt_deg", s.tiltDeg);
        cJSON_AddNumberToObject(doc, "tilt_limit_deg", s.tiltLimitDeg);
        cJSON_AddNumberToObject(doc, "tilt_limit_count", s.tiltLimitCount);
        cJSON_AddStringToObject(doc, "tilt_limit", s.tiltLimitReason);
        char *json = cJSON_PrintUnformatted(doc);
        cJSON_Delete(doc);
        if (json) { sendText(sock, json); sendText(sock, "\r\n"); cJSON_free(json); }
    } else {
        sendText(sock, command_execute(line) ? "ACK:OK\r\n" : "ERR:REJECTED\r\n");
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
                if (!command_execute(command)) printf("ERR:REJECTED\n");
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
