#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t touch_init(void);
// Samples the panel. Returns true while pressed and stores portrait screen coordinates.
bool touch_read(int *x, int *y);
