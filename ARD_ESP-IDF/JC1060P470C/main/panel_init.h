#pragma once
#include <stddef.h>
#include "esp_lcd_jd9165.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const esp_lcd_video_timing_t ard_panel_timing;
extern const float ard_panel_pclk_mhz;
extern const jd9165_lcd_init_cmd_t ard_panel_init[];
extern const size_t ard_panel_init_count;
extern const char *const ard_panel_name;

#ifdef __cplusplus
}
#endif
