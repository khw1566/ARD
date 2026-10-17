#include "inputs.h"
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SCAN_MS      5
#define STABLE_SCANS 4  // 20 ms of agreement before a level counts

static const char *TAG = "INPUT";

enum { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_TOG_A, IN_TOG_B, IN_BTN, IN_COUNT };
static const int pins[IN_COUNT] = {PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT,
                                   PIN_TOGGLE_A, PIN_TOGGLE_B, PIN_VOICE_BTN};

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static control_state_t current = {.mode = 'N', .sub_mode = 1};
static inputs_change_cb_t change_cb;

bool inputs_any_joy(const control_state_t *s) {
    for (int i = 0; i < JOY_COUNT; ++i)
        if (s->joy[i]) return true;
    return false;
}

void inputs_get(control_state_t *out) {
    taskENTER_CRITICAL(&lock);
    *out = current;
    taskEXIT_CRITICAL(&lock);
}

static control_state_t decode(const bool pressed[IN_COUNT]) {
    control_state_t s = {0};
    s.mode = pressed[IN_TOG_A] ? 'A' : pressed[IN_TOG_B] ? 'B' : 'N';
    // The switch latches, so the sub mode follows its level. The Arduino build
    // reset sub mode to 1 on every mode change and could then disagree with a
    // button that was still held down until the next press.
    s.sub_mode = pressed[IN_BTN] ? 2 : 1;
    for (int i = 0; i < JOY_COUNT; ++i) s.joy[i] = pressed[IN_UP + i];
    return s;
}

static void scan_task(void *arg) {
    bool stable[IN_COUNT], last_raw[IN_COUNT];
    int count[IN_COUNT] = {0};
    for (int i = 0; i < IN_COUNT; ++i) stable[i] = last_raw[i] = gpio_get_level(pins[i]) == 0;
    taskENTER_CRITICAL(&lock);
    current = decode(stable);
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Mode:%c Sub:%d", current.mode, current.sub_mode);

    TickType_t wake = xTaskGetTickCount();
    while (true) {
        bool changed = false;
        for (int i = 0; i < IN_COUNT; ++i) {
            bool raw = gpio_get_level(pins[i]) == 0;
            count[i] = raw == last_raw[i] ? count[i] + 1 : 0;
            last_raw[i] = raw;
            if (raw != stable[i] && count[i] >= STABLE_SCANS - 1) {
                stable[i] = raw;
                changed = true;
            }
        }
        if (changed) {
            control_state_t next = decode(stable), prev;
            taskENTER_CRITICAL(&lock);
            prev = current;
            current = next;
            taskEXIT_CRITICAL(&lock);
            if (memcmp(&prev, &next, sizeof(next))) {
                if (prev.sub_mode != next.sub_mode)
                    ESP_LOGI(TAG, "Voice button %s", next.sub_mode == 2 ? "pressed" : "released");
                ESP_LOGI(TAG, "Mode:%c Sub:%d Joy:%d%d%d%d", next.mode, next.sub_mode, next.joy[0], next.joy[1],
                         next.joy[2], next.joy[3]);
                if (change_cb) change_cb(&next);
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SCAN_MS));
    }
}

void inputs_start(inputs_change_cb_t on_change) {
    change_cb = on_change;
    uint64_t mask = 0;
    for (int i = 0; i < IN_COUNT; ++i) mask |= 1ULL << pins[i];
    gpio_config_t cfg = {.pin_bit_mask = mask, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&cfg));
    configASSERT(xTaskCreatePinnedToCore(scan_task, "inputs", 3072, NULL, 6, NULL, 1) == pdPASS);
}
