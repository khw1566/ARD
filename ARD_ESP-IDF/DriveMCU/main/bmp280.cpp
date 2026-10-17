#include "bmp280.h"
#include "ard_wire.h"

bool Bmp280::readRegs(uint8_t reg, uint8_t *out, uint8_t count) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    if (Wire.endTransmission() != 0 || Wire.requestFrom(address_, count) != count) return false;
    for (uint8_t i = 0; i < count; ++i) out[i] = (uint8_t)Wire.read();
    return true;
}

bool Bmp280::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool Bmp280::begin(uint8_t address) {
    address_ = address;
    uint8_t id = 0;
    if (!readRegs(0xD0, &id, 1) || id != 0x58) return false;
    if (!writeReg(0xE0, 0xB6)) return false; // soft reset
    delay(5);
    uint8_t status = 1;
    for (int i = 0; i < 20 && (status & 0x01); ++i) { // wait for NVM copy
        if (!readRegs(0xF3, &status, 1)) return false;
        delay(2);
    }
    uint8_t c[24];
    if (!readRegs(0x88, c, sizeof(c))) return false;
    auto u16 = [&](int i) { return (uint16_t)(c[i] | (c[i + 1] << 8)); };
    t1_ = u16(0);  t2_ = (int16_t)u16(2);  t3_ = (int16_t)u16(4);
    p1_ = u16(6);  p2_ = (int16_t)u16(8);  p3_ = (int16_t)u16(10);
    p4_ = (int16_t)u16(12); p5_ = (int16_t)u16(14); p6_ = (int16_t)u16(16);
    p7_ = (int16_t)u16(18); p8_ = (int16_t)u16(20); p9_ = (int16_t)u16(22);
    return writeReg(0xF5, 0x00) && writeReg(0xF4, (5 << 5) | (5 << 2) | 3);
}

// Bosch datasheet integer compensation (section 3.11.3).
bool Bmp280::readPressure(float &hPa) {
    uint8_t d[6];
    if (!readRegs(0xF7, d, sizeof(d))) return false;
    const int32_t adcP = (int32_t)((d[0] << 12) | (d[1] << 4) | (d[2] >> 4));
    const int32_t adcT = (int32_t)((d[3] << 12) | (d[4] << 4) | (d[5] >> 4));
    if (adcP == 0x80000 || adcT == 0x80000) return false; // measurement skipped

    int32_t v1 = ((((adcT >> 3) - ((int32_t)t1_ << 1))) * (int32_t)t2_) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)t1_) * ((adcT >> 4) - (int32_t)t1_)) >> 12) * (int32_t)t3_) >> 14;
    const int32_t tFine = v1 + v2;

    int64_t a = (int64_t)tFine - 128000;
    int64_t b = a * a * (int64_t)p6_;
    b += (a * (int64_t)p5_) << 17;
    b += ((int64_t)p4_) << 35;
    a = ((a * a * (int64_t)p3_) >> 8) + ((a * (int64_t)p2_) << 12);
    a = ((((int64_t)1) << 47) + a) * (int64_t)p1_ >> 33;
    if (a == 0) return false;
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - b) * 3125) / a;
    a = ((int64_t)p9_ * (p >> 13) * (p >> 13)) >> 25;
    b = ((int64_t)p8_ * p) >> 19;
    p = ((p + a + b) >> 8) + (((int64_t)p7_) << 4);
    hPa = (float)p / 25600.0f;
    return hPa > 300.0f && hPa < 1100.0f;
}
