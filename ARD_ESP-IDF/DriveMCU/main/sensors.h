#pragma once
// Sensor task: the only owner of the I2C bus (AHT20, BMP280, VL53L0X, SSD1306).
// Other tasks read cached snapshots, never the bus.
#include <stdint.h>
#include "esp_err.h"

struct EnvironmentSample {
    float temperature = 0, humidity = 0, pressure = 0;
    bool ahtValid = false, bmpValid = false;
};

esp_err_t sensors_start();
EnvironmentSample sensors_environment();
bool sensors_tof_ready();
bool sensors_tof(float *mm, uint32_t *ageMs); // false until a valid sample exists
float sensors_tof_last();                     // filtered distance, 0 before the first sample
const char *sensors_tof_status();
void sensors_request_i2c_clock(uint32_t hz);
bool sensors_oled_ready();
