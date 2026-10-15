#pragma once
#include "Arduino.h"
struct FakeWire {
    bool missing = false, busy = false;
    unsigned reads = 0, writes = 0, index = 0, size = 0;
    uint8_t bytes[6] = {8, 128, 0, 8, 0, 0};
    void beginTransmission(uint8_t) {}
    void write(const uint8_t*, size_t) { ++writes; }
    int endTransmission() { return missing ? 2 : 0; }
    int requestFrom(uint8_t, uint8_t count) { ++reads; index=0; size=count; return missing ? 0 : count; }
    uint8_t read() { return size==1 ? (busy ? 0x80 : 0x08) : bytes[index++]; }
};
extern FakeWire Wire;
