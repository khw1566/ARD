// Run the firmware's actual band renderer on an emulated ESP32. No radio or motor IO.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi_ui.h"
#include "hmi_port.h"
#include "hmi_setup.h"
#include "ard_setup_qr.h"

// ESP32 RAM consists of separate heap regions; keep captures in small row bands.
static uint16_t *pixels[HMI_H / 40];
static uint16_t band[HMI_W * 40];
static hmi_model_t model;
static uint32_t millis = 1000;
void run_wifi_worker_tests(void);

static uint16_t *acquire(void *user) { return band; }
static void flush(void *user, int x, int y, int w, int h, uint16_t *data) {
    assert(x == 0 && w == HMI_W && y >= 0 && y + h <= HMI_H);
    for (int row = 0; row < h; ++row)
        memcpy(pixels[(y + row) / 40] + ((y + row) % 40) * HMI_W, data + row * w, w * sizeof(*data));
}
static const gfx_target_t target = {.width = HMI_W, .height = HMI_H, .band_h = 40, .acquire = acquire, .flush = flush};
const char *ard_network_hostname(void) { return "ard-hmi-123456"; }
const char *ard_network_password(void) { return ""; }
uint32_t hmi_millis(void) { return millis; }
bool hmi_clock(char *out, size_t size) { snprintf(out, size, "12:34"); return true; }
void hmi_model_get(hmi_model_t *out) { *out = model; }
void hmi_model_edit(void (*fn)(hmi_model_t *, void *), void *arg) { fn(&model, arg); ++model.revision; }
void hmi_send_command(hmi_target_t target, const char *command) { printf("[SIM_COMMAND] %d %s\n", target, command); }
void hmi_send_approval(const char *id, bool approve) { }
void hmi_request_refresh(void) { }
void hmi_image_want(bool wanted) { }
const gfx_image_t *hmi_image_acquire(uint32_t *seq, const char **error) { *seq = 0; *error = "No preview"; return NULL; }
void hmi_image_release(void) { }

static void capture(const char *name) {
    static const char hex[] = "0123456789abcdef";
    char row[HMI_W * 4 + 1];
    printf("[SIM_FRAME %s]\n", name);
    for (int y = 0; y < HMI_H; ++y) {
        for (int x = 0; x < HMI_W; ++x) {
            uint16_t color = GFX_BE(pixels[y / 40][(y % 40) * HMI_W + x]);
            for (int j = 0; j < 4; ++j) row[x * 4 + j] = hex[(color >> (12 - j * 4)) & 15];
        }
        row[HMI_W * 4] = 0;
        puts(row);
    }
    puts("[SIM_END_FRAME]");
}

void app_main(void) {
    for (int i = 0; i < HMI_H / 40; ++i) {
        pixels[i] = malloc(HMI_W * 40 * sizeof(**pixels));
        assert(pixels[i]);
    }
    run_wifi_worker_tests();
    hmi_model_defaults(&model);
    model.wifi_ok = model.server_ok = true;
    model.wifi_rssi = -45;
    strcpy(model.server, "192.168.0.35");
    for (int i = 0; i < DEV_COUNT; ++i) {
        model.dev[i].online = true;
        model.dev[i].rssi = -50;
        strcpy(model.dev[i].ip, "192.168.0.40");
        strcpy(model.dev[i].last_seen, "12:34:00");
    }
    hmi_ui_init(&target);
    hmi_ui_start();
    capture("network");
    hmi_ui_touch(215, 280);
    millis += 200;
    hmi_ui_tick();
    capture("drive");
    ard_setup_qr_init(ard_network_hostname(), ard_network_password());
    hmi_setup_show(&target, true);
    capture("wifi-qr");
    hmi_setup_show(&target, false);
    capture("web-qr");
    hmi_ui_start();
    capture("returned");
    puts("[SIM_DONE]");
    fflush(stdout);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
