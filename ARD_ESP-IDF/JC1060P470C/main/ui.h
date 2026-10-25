#pragma once
// 1024x600 LVGL UI: status bar with a permanent E-STOP, tabs Main / Drive / Cam / Voice / System /
// Settings, and the AI approval popup. Replaces the 240x320 HMI_MCU screen.
#include "esp_err.h"

esp_err_t ui_start(); // after bsp_display_start()
// Full-width notice above every tab (e.g. a firmware update in progress); nullptr hides it.
void ui_notice(const char *text);
