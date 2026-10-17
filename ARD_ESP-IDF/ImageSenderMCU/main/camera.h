#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_camera.h"

// Only the capture task calls these; they are not thread-safe.
esp_err_t camera_start(const char *resolution, int quality);
// Re-initialises on resolution change, applies quality live otherwise.
esp_err_t camera_apply(const char *resolution, int quality);
// Returns a frame exposed after this call (one stale buffer is discarded).
camera_fb_t *camera_capture_fresh(void);
const char *camera_sensor_name(void);
const char *camera_resolution(void);
int camera_quality(void);
