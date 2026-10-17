#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct gptimer_t *gptimer_handle_t;
typedef struct { uint64_t count_value, alarm_value; } gptimer_alarm_event_data_t;
typedef bool (*gptimer_alarm_cb_t)(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *);
typedef struct { gptimer_alarm_cb_t on_alarm; } gptimer_event_callbacks_t;
enum { GPTIMER_CLK_SRC_DEFAULT = 0, GPTIMER_COUNT_UP = 0 };
typedef struct { int clk_src, direction; uint32_t resolution_hz; int intr_priority; } gptimer_config_t;
typedef struct { uint64_t alarm_count, reload_count; struct { uint32_t auto_reload_on_alarm : 1; } flags; } gptimer_alarm_config_t;
inline esp_err_t gptimer_new_timer(const gptimer_config_t *, gptimer_handle_t *h) { *h = nullptr; return ESP_OK; }
inline esp_err_t gptimer_register_event_callbacks(gptimer_handle_t, const gptimer_event_callbacks_t *, void *) { return ESP_OK; }
inline esp_err_t gptimer_set_alarm_action(gptimer_handle_t, const gptimer_alarm_config_t *) { return ESP_OK; }
inline esp_err_t gptimer_enable(gptimer_handle_t) { return ESP_OK; }
inline esp_err_t gptimer_start(gptimer_handle_t) { return ESP_OK; }
