#include "cmd_server.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "audio.h"
#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inputs.h"
#include "led.h"
#include "lwip/sockets.h"

#define LINE_MAX_LEN 64
#define IDLE_CLOSE_S 5 // a silent client no longer blocks the next one

static const char *TAG = "CMD";

static bool parse_volume(const char *s, int *out) {
    if (!isdigit((unsigned char)*s)) return false;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end || v < 0 || v > AUDIO_VOLUME_MAX) return false;
    *out = (int)v;
    return true;
}

void cmd_execute(const char *line, char *reply, size_t size) {
    audio_status_t a;
    int v;
    if (!strncasecmp(line, "vol=", 4) || !strncasecmp(line, "volume=", 7)) {
        if (parse_volume(strchr(line, '=') + 1, &v)) snprintf(reply, size, "Volume set to %d", audio_set_volume(v));
        else snprintf(reply, size, "Error: Volume must be between 0 and %d", AUDIO_VOLUME_MAX);
    } else if (!strcasecmp(line, "vol+") || !strcasecmp(line, "vol-")) {
        audio_get_status(&a);
        snprintf(reply, size, "Volume set to %d", audio_set_volume(a.volume + (line[3] == '+' ? 1 : -1)));
    } else if (!strcasecmp(line, "status")) {
        control_state_t s;
        inputs_get(&s);
        audio_get_status(&a);
        // Starts with "vol=N" like the Arduino reply, so older parsers keep working.
        snprintf(reply, size, "vol=%d muted=%d playing=%d mode=%c sub=%d", a.volume, a.muted, a.playing, s.mode,
                 s.sub_mode);
    } else if (!strcasecmp(line, "stop")) {
        audio_stop();
        snprintf(reply, size, "Audio stopped");
    } else if (!strcasecmp(line, "mute")) {
        audio_set_muted(true);
        snprintf(reply, size, "OK");
    } else if (!strcasecmp(line, "play") || !strcasecmp(line, "unmute")) {
        audio_set_muted(false);
        snprintf(reply, size, "OK");
    } else if (!strcasecmp(line, "mic_ready") || !strcasecmp(line, "mic_done")) {
        led_mic(line[4] == 'r' || line[4] == 'R');
        snprintf(reply, size, "OK");
    } else if (!strncasecmp(line, "radio=", 6)) {
        // The PC decodes radio (ffmpeg) and streams PCM here; the board no longer does.
        snprintf(reply, size, "Error: radio plays from the PC (Media tab)");
    } else {
        snprintf(reply, size, "Error: unknown command");
    }
}

static void handle_client(int fd) {
    struct timeval tv = {.tv_sec = IDLE_CLOSE_S};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[128], line[LINE_MAX_LEN + 1], reply[96];
    size_t len = 0;
    bool overflow = false;
    while (true) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (n < 0 || !len) break;
            buf[0] = '\n'; // peer closed after a last line without newline: run it
            n = 1;
        }
        for (int i = 0; i < n; ++i) {
            char c = buf[i];
            if (c != '\n' && c != '\r') {
                if (len < LINE_MAX_LEN) line[len++] = c;
                else overflow = true;
                continue;
            }
            while (len && isspace((unsigned char)line[len - 1])) len--;
            line[len] = 0;
            char *cmd = line;
            while (isspace((unsigned char)*cmd)) cmd++;
            if (*cmd) {
                if (overflow) snprintf(reply, sizeof(reply), "Error: line too long");
                else cmd_execute(cmd, reply, sizeof(reply));
                ESP_LOGI(TAG, "%s -> %s", cmd, reply);
                strlcat(reply, "\n", sizeof(reply));
                send(fd, reply, strlen(reply), 0);
            }
            len = 0;
            overflow = false;
        }
    }
}

static void server_task(void *arg) {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(COMMAND_TCP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (listener < 0 || bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(listener, 4) < 0) {
        ESP_LOGE(TAG, "TCP %d listen failed", COMMAND_TCP_PORT);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "Listening on TCP %d", COMMAND_TCP_PORT);
    while (true) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        handle_client(fd);
        close(fd);
    }
}

void cmd_server_start(void) {
    configASSERT(xTaskCreatePinnedToCore(server_task, "cmd_tcp", 4096, NULL, 5, NULL, 0) == pdPASS);
}
