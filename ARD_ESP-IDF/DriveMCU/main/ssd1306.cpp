#include "ssd1306.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ard_wire.h"
#include "third_party/adafruit_gfx/glcdfont.h"

bool Ssd1306::command(const uint8_t *cmds, int count) {
    uint8_t packet[32];
    if (count > (int)sizeof(packet) - 1) return false;
    packet[0] = 0x00; // control byte: command stream
    memcpy(packet + 1, cmds, count);
    return Wire.transmit(address_, packet, count + 1) == ESP_OK;
}

bool Ssd1306::begin(uint8_t address) {
    address_ = address;
    // Adafruit_SSD1306 128x64 SWITCHCAPVCC init sequence.
    static const uint8_t init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF,
    };
    if (!command(init, sizeof(init))) return false;
    clear();
    commit();
    while (dirty_) if (!flushNextPage()) return false;
    return true;
}

void Ssd1306::clear() {
    memset(buffer_, 0, sizeof(buffer_));
    cx_ = cy_ = 0;
    inverse_ = false;
}

void Ssd1306::pixel(int x, int y, bool on) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    uint8_t &cell = buffer_[x + (y / 8) * kWidth];
    if (on) cell |= 1 << (y & 7); else cell &= ~(1 << (y & 7));
}

void Ssd1306::hline(int x, int y, int w) {
    for (int i = 0; i < w; ++i) pixel(x + i, y, true);
}

void Ssd1306::drawChar(char c) {
    if (c == '\n') { cx_ = 0; cy_ += 8; return; }
    if (c == '\r') return;
    const uint8_t code = (uint8_t)c;
    for (int col = 0; col < 6; ++col) {
        const uint8_t bits = col < 5 ? font[code * 5 + col] : 0;
        for (int row = 0; row < 8; ++row) pixel(cx_ + col, cy_ + row, ((bits >> row) & 1) != inverse_);
    }
    cx_ += 6;
}

void Ssd1306::print(const char *text) {
    while (*text) drawChar(*text++);
}

void Ssd1306::printf(const char *format, ...) {
    char line[48];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    print(line);
}

bool Ssd1306::flushNextPage() {
    if (!dirty_) return false;
    int page = 0;
    while (!(dirty_ & (1 << page))) ++page;
    const uint8_t window[] = {0x21, 0, kWidth - 1, 0x22, (uint8_t)page, (uint8_t)page};
    uint8_t packet[kWidth + 1];
    packet[0] = 0x40; // control byte: data stream
    memcpy(packet + 1, buffer_ + page * kWidth, kWidth);
    if (!command(window, sizeof(window)) || Wire.transmit(address_, packet, sizeof(packet)) != ESP_OK) return false;
    dirty_ &= ~(1 << page);
    return true;
}
