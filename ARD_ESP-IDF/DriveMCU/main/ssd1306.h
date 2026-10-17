#pragma once
// 128x64 SSD1306 over I2C with a RAM frame buffer and the Adafruit 5x7 font.
// flushPage() sends one of the 8 pages (129 bytes, ~12 ms at 100 kHz) so the sensor
// task can interleave the display with ToF/AHT20 reads instead of blocking ~90 ms.
#include <stdint.h>

class Ssd1306 {
public:
    static constexpr int kWidth = 128, kHeight = 64, kPages = 8;

    bool begin(uint8_t address = 0x3C);
    void clear();
    void setCursor(int x, int y) { cx_ = x; cy_ = y; }
    void setInverse(bool inverse) { inverse_ = inverse; }
    void hline(int x, int y, int w);
    void print(const char *text);
    void printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void commit() { dirty_ = 0xFF; } // mark the whole frame for transfer
    bool flushNextPage();            // false when nothing was pending or on error

private:
    void pixel(int x, int y, bool on);
    void drawChar(char c);
    bool command(const uint8_t *cmds, int count);

    uint8_t address_ = 0x3C;
    uint8_t buffer_[kWidth * kPages] = {};
    uint8_t dirty_ = 0;
    int cx_ = 0, cy_ = 0;
    bool inverse_ = false;
};
