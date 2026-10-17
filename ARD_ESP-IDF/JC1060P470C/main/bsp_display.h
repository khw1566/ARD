#pragma once
// JC1060P470C display stack: MIPI-DSI JD9165 panel, GT911 touch, backlight PWM and LVGL 9.
// LVGL is driven by its own task; any other task must wrap LVGL calls in bsp_lvgl_lock/unlock.
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// Creates the board I2C bus (SDA 7 / SCL 8). Touch and the external sensors share it.
esp_err_t bsp_i2c_init(i2c_master_bus_handle_t *out);

// Panel + touch + LVGL. Touch failure is logged and the display still starts.
esp_err_t bsp_display_start(i2c_master_bus_handle_t bus);

void bsp_backlight_set(int percent); // 0..100
int bsp_backlight_get();

bool bsp_lvgl_lock(uint32_t timeoutMs);
void bsp_lvgl_unlock();
bool bsp_touch_ready();
