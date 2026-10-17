#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "gfx.h"

// Decodes a baseline JPEG with the ESP32 ROM TJpgDec into a newly allocated
// big-endian RGB565 image, downscaled by 1/2/4/8 until it fits max_w x max_h.
// Free out->px with free().
esp_err_t jpeg_decode(const uint8_t *data, size_t len, int max_w, int max_h, gfx_image_t *out);
