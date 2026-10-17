#pragma once
// Ported from DriveMCU/include/BoundedAht20.h; Wire is the i2c_master shim.
#include "ard_wire.h"

// The upstream getEvent/begin busy loops have no deadline. Keep missing or
// stuck AHT20 hardware from holding the UI loop indefinitely.
class BoundedAht20 {
public:
    bool begin() {
        const uint8_t reset[] = {0xBA};
        const uint8_t calibrate[] = {0xBE, 0x08, 0x00};
        if (!write(reset, sizeof(reset))) return false;
        delay(20);
        if (!waitReady() || !write(calibrate, sizeof(calibrate))) return false;
        delay(10);
        uint8_t status;
        return waitReady() && readStatus(status) && (status & 0x08);
    }
    bool poll(float &temperature, float &humidity) {
        const uint32_t now = millis();
        if (!measuring) {
            if (started && now - started < 5000) return false;
            const uint8_t trigger[] = {0xAC, 0x33, 0x00};
            started = now;
            measuring = write(trigger, sizeof(trigger));
            return false;
        }
        if (now - started < 80) return false;
        uint8_t status;
        if (!readStatus(status) || now - started > 250) { measuring = false; valid = false; return false; }
        if (status & 0x80) return false;
        measuring = false;
        if (Wire.requestFrom(uint8_t(0x38), uint8_t(6)) != 6) return false;
        uint8_t data[6];
        for (auto &byte : data) byte = Wire.read();
        if (data[0] & 0x80) return false;
        const uint32_t rawHumidity = (uint32_t(data[1]) << 12) | (uint32_t(data[2]) << 4) | (data[3] >> 4);
        const uint32_t rawTemperature = (uint32_t(data[3] & 0x0F) << 16) | (uint32_t(data[4]) << 8) | data[5];
        humidity = rawHumidity * (100.0f / 1048576.0f);
        temperature = rawTemperature * (200.0f / 1048576.0f) - 50.0f;
        lastSuccess = now;
        valid = true;
        return true;
    }
    bool healthy() const { return valid && millis() - lastSuccess < 10000; }
private:
    uint32_t started = 0;
    uint32_t lastSuccess = 0;
    bool valid = false;
    bool measuring = false;
    bool write(const uint8_t *data, size_t size) {
        Wire.beginTransmission(0x38);
        Wire.write(data, size);
        return Wire.endTransmission() == 0;
    }
    bool readStatus(uint8_t &status) {
        if (Wire.requestFrom(uint8_t(0x38), uint8_t(1)) != 1) return false;
        status = Wire.read();
        return true;
    }
    bool waitReady() {
        const uint32_t start = millis();
        do {
            uint8_t status;
            if (!readStatus(status)) return false;
            if (!(status & 0x80)) return true;
            delay(10);
        } while (millis() - start < 200);
        return false;
    }
};
