#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define LCD_W 240
#define LCD_H 320
// Rows per DMA band. Two bands are double-buffered: one renders while the other transmits.
#define LCD_BAND_H 40

esp_err_t lcd_init(bool invert);
void lcd_backlight(bool on);
// Blocks until a band buffer (LCD_W * LCD_BAND_H pixels, big-endian RGB565) is free.
uint16_t *lcd_acquire_band(void);
// Queues the window for DMA; the buffer returns to the pool when the transfer finishes.
void lcd_flush(int x, int y, int w, int h, uint16_t *pixels);
