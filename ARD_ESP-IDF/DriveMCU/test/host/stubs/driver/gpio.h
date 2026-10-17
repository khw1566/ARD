#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef enum { GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
typedef struct { uint64_t pin_bit_mask; gpio_mode_t mode; } gpio_config_t;
inline esp_err_t gpio_config(const gpio_config_t *) { return ESP_OK; }
inline esp_err_t gpio_set_level(gpio_num_t, uint32_t) { return ESP_OK; }
