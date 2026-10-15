#pragma once
#include <cstdint>
#include <cstddef>
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t ms) { testMillis += ms; }
