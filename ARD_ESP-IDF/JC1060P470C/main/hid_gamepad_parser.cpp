#include "hid_gamepad_parser.h"
#include <string.h>

namespace {

constexpr uint16_t kPageDesktop = 0x01, kPageSimulation = 0x02, kPageButton = 0x09;

int32_t signedValue(uint32_t raw, int size) {
    if (size == 1) return (int8_t)raw;
    if (size == 2) return (int16_t)raw;
    return (int32_t)raw;
}

uint32_t bits(const uint8_t *data, size_t length, unsigned offset, unsigned size) {
    uint32_t value = 0;
    for (unsigned i = 0; i < size && i < 32; ++i) {
        const unsigned bit = offset + i;
        if (bit / 8 >= length) break;
        if (data[bit / 8] & (1u << (bit % 8))) value |= 1u << i;
    }
    return value;
}

float axis(int32_t v, int32_t lo, int32_t hi) {
    if (hi <= lo) return 0;
    float f = ((float)(v - lo) / (float)(hi - lo)) * 2.0f - 1.0f;
    return f < -1 ? -1 : f > 1 ? 1 : f;
}

} // namespace

void HidGamepadParser::add(const Field &field) {
    if (count_ < kMaxFields) fields_[count_++] = field;
}

int HidGamepadParser::parse(const uint8_t *d, size_t length) {
    struct Globals { uint16_t page = 0; int32_t lmin = 0, lmax = 0; uint32_t size = 0, count = 0; uint8_t id = 0; };
    Globals g, stack[4];
    int depth = 0;
    uint32_t usages[16];
    int usageCount = 0;
    uint32_t usageMin = 0, usageMax = 0;
    bool haveRange = false;
    uint16_t offsets[256] = {}; // input bit offset per report ID
    count_ = 0;
    hasRxRy_ = false;

    for (size_t i = 0; i < length;) {
        const uint8_t prefix = d[i];
        if (prefix == 0xFE) { // long item
            if (i + 1 >= length) break;
            i += 3 + d[i + 1];
            continue;
        }
        const int size = (prefix & 3) == 3 ? 4 : (prefix & 3);
        const int type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (i + 1 + size > length) break;
        uint32_t raw = 0;
        for (int b = 0; b < size; ++b) raw |= (uint32_t)d[i + 1 + b] << (8 * b);
        i += 1 + size;

        if (type == 1) { // global
            switch (tag) {
            case 0: g.page = (uint16_t)raw; break;
            case 1: g.lmin = signedValue(raw, size); break;
            case 2: g.lmax = signedValue(raw, size); if (g.lmin >= 0 && g.lmax < 0) g.lmax = (int32_t)raw; break;
            case 7: g.size = raw; break;
            case 8: g.id = (uint8_t)raw; break;
            case 9: g.count = raw; break;
            case 10: if (depth < 4) stack[depth++] = g; break;
            case 11: if (depth > 0) g = stack[--depth]; break;
            }
        } else if (type == 2) { // local
            if (tag == 0 && usageCount < 16) usages[usageCount++] = size == 4 ? raw : ((uint32_t)g.page << 16) | raw;
            else if (tag == 1) { usageMin = size == 4 ? raw : ((uint32_t)g.page << 16) | raw; haveRange = true; }
            else if (tag == 2) { usageMax = size == 4 ? raw : ((uint32_t)g.page << 16) | raw; haveRange = true; }
        } else if (type == 0) { // main
            if (tag == 8) { // Input
                const bool constant = raw & 1, variable = raw & 2;
                uint16_t &offset = offsets[g.id];
                for (uint32_t n = 0; n < g.count; ++n, offset += g.size) {
                    if (constant || !variable || g.size == 0 || g.size > 32) continue;
                    uint32_t usage;
                    if (haveRange) usage = usageMin + n;
                    else if (usageCount) usage = usages[(int)n < usageCount ? n : usageCount - 1];
                    else continue;
                    if (haveRange && usage > usageMax) continue;
                    const uint16_t page = usage >> 16, id = usage & 0xFFFF;
                    Field f = {g.id, AXIS_X, 0, offset, (uint8_t)g.size, g.lmin, g.lmax};
                    if (page == kPageDesktop && id >= 0x30 && id <= 0x35) {
                        f.kind = (Kind)(AXIS_X + (id - 0x30));
                        if (id == 0x33 || id == 0x34) hasRxRy_ = true;
                    } else if (page == kPageDesktop && id == 0x39) f.kind = HAT;
                    else if (page == kPageButton && id >= 1 && id <= 32) { f.kind = BUTTON; f.button = (uint8_t)id; }
                    else if (page == kPageSimulation && id == 0xC5) f.kind = TRIGGER_L; // Brake
                    else if (page == kPageSimulation && id == 0xC4) f.kind = TRIGGER_R; // Accelerator
                    else continue;
                    add(f);
                }
            }
            usageCount = 0;
            haveRange = false;
        }
    }
    return count_;
}

bool HidGamepadParser::decode(uint8_t reportId, const uint8_t *data, size_t length, GamepadState &s) const {
    bool matched = false;
    for (int i = 0; i < count_; ++i) {
        const Field &f = fields_[i];
        if (f.reportId != reportId) continue;
        if ((f.bitOffset + f.bitSize + 7) / 8 > length) continue;
        matched = true;
        const uint32_t raw = bits(data, length, f.bitOffset, f.bitSize);
        int32_t v = (int32_t)raw;
        if (f.logicalMin < 0 && f.bitSize < 32 && (raw & (1u << (f.bitSize - 1)))) v = (int32_t)(raw | (~0u << f.bitSize));
        switch (f.kind) {
        case AXIS_X: s.lx = axis(v, f.logicalMin, f.logicalMax); break;
        case AXIS_Y: s.ly = axis(v, f.logicalMin, f.logicalMax); break;
        case AXIS_Z: if (!hasRxRy_) s.rx = axis(v, f.logicalMin, f.logicalMax); else s.lt = (axis(v, f.logicalMin, f.logicalMax) + 1) / 2; break;
        case AXIS_RZ: if (!hasRxRy_) s.ry = axis(v, f.logicalMin, f.logicalMax); else s.rt = (axis(v, f.logicalMin, f.logicalMax) + 1) / 2; break;
        case AXIS_RX: s.rx = axis(v, f.logicalMin, f.logicalMax); break;
        case AXIS_RY: s.ry = axis(v, f.logicalMin, f.logicalMax); break;
        case TRIGGER_L: s.lt = (axis(v, f.logicalMin, f.logicalMax) + 1) / 2; break;
        case TRIGGER_R: s.rt = (axis(v, f.logicalMin, f.logicalMax) + 1) / 2; break;
        case HAT: {
            const int32_t span = f.logicalMax - f.logicalMin;
            if (v < f.logicalMin || v > f.logicalMax) s.hat = -1;
            else s.hat = span == 3 ? (v - f.logicalMin) * 2 : (v - f.logicalMin) % 8;
            break;
        }
        case BUTTON:
            if (raw) s.buttons |= 1u << (f.button - 1); else s.buttons &= ~(1u << (f.button - 1));
            break;
        }
    }
    return matched;
}
