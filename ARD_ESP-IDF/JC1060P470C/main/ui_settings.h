#pragma once
#include "esp_err.h"
#include "lvgl.h"

// Called with the BSP LVGL lock held. All Wi-Fi/NVS operations run on a worker.
esp_err_t ui_settings_create(lv_obj_t *page);
void ui_settings_poll(bool visible);
