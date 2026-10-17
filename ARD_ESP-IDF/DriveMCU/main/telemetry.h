#pragma once
// PC link over HTTP (port 5000): /api/log, /api/heartbeat, /api/time (A-GPS).
#include <stdint.h>
#include "esp_err.h"

esp_err_t telemetry_start();
void log_async(const char *format, ...) __attribute__((format(printf, 1, 2)));
void telemetry_set_heartbeat_interval(uint32_t ms);
void telemetry_request_gps_sync();
uint32_t telemetry_dropped_logs();
