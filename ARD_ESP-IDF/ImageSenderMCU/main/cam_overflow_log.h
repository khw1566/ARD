#pragma once
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"

// Both callers run on the driver's camera task, never in the camera ISR.
static inline void ard_camera_report_overflow(void) {
    static int64_t next_report;
    static uint32_t pending;
    if (pending < UINT32_MAX) ++pending;
    int64_t now = esp_timer_get_time();
    if (now < next_report) return;
    ESP_LOGW("cam_hal", "FB-OVF: %lu frame buffer overflow(s) since last report; reports limited to once per 5 s",
             (unsigned long)pending);
    pending = 0;
    next_report = now + 5000000;
}
