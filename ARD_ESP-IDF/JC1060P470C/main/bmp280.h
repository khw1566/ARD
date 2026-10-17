#pragma once
// Small BMP280 driver (replaces Adafruit_BMP280). Normal mode, x16 oversampling,
// filter off, 0.5 ms standby: the same settings as Adafruit's begin().
#include <stdint.h>

class Bmp280 {
public:
    bool begin(uint8_t address);
    bool readPressure(float &hPa);
    uint8_t address() const { return address_; }

private:
    bool readRegs(uint8_t reg, uint8_t *out, uint8_t count);
    bool writeReg(uint8_t reg, uint8_t value);

    uint8_t address_ = 0;
    uint16_t t1_ = 0, p1_ = 0;
    int16_t t2_ = 0, t3_ = 0, p2_ = 0, p3_ = 0, p4_ = 0, p5_ = 0, p6_ = 0, p7_ = 0, p8_ = 0, p9_ = 0;
};
