#pragma once
#include <stdint.h>
// Records output levels written through the w1ts/w1tc registers.
struct GpioSim {
    uint32_t level = 0;
    struct Reg {
        GpioSim *owner; bool set;
        Reg &operator=(uint32_t mask) { if (set) owner->level |= mask; else owner->level &= ~mask; owner->onWrite(mask, set); return *this; }
    };
    Reg out_w1ts{this, true}, out_w1tc{this, false};
    void (*hook)(uint32_t mask, bool set) = nullptr;
    void onWrite(uint32_t mask, bool set) { if (hook) hook(mask, set); }
};
extern GpioSim GPIO;
