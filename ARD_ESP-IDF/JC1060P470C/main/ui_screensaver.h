#pragma once
#include "esp_err.h"
#include "lvgl.h"

// Called only with the LVGL lock held. Zero seconds disables automatic entry.
void ui_screensaver_init(void (*onEnter)(), void (*onWake)());
void ui_screensaver_enter(lv_event_t *event = nullptr);
void ui_screensaver_poll(bool blocked);
bool ui_screensaver_active();
uint32_t ui_screensaver_timeout();
esp_err_t ui_screensaver_set_timeout(uint32_t seconds);
