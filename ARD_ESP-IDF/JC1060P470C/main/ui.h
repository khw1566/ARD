#pragma once
// 1024x600 LVGL UI: status bar with a permanent E-STOP, tabs Drive / Robot / Cam / Sound / System,
// and the AI approval popup. Replaces the 240x320 HMI_MCU screen.
#include "esp_err.h"

esp_err_t ui_start(); // after bsp_display_start()
