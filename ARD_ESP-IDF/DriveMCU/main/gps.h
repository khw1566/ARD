#pragma once
// NEO-6M on UART2: NMEA RMC/GGA parser (replaces TinyGPSPlus) and UBX output.
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

struct GpsSnapshot {
    bool enabled;
    bool fix;          // a valid RMC/GGA fix seen within the last 5 s
    double lat, lng;
    uint32_t satellites;
    uint32_t chars;    // bytes processed while enabled
};

esp_err_t gps_start();
void gps_set_enabled(bool enabled);
bool gps_enabled();
GpsSnapshot gps_snapshot();
void gps_send(const uint8_t *data, size_t size);
