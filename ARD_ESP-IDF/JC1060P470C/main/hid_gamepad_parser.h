#pragma once
// Minimal HID report-descriptor parser for gamepads/joysticks. It reads the descriptor once,
// records where the axes, hat switch and buttons live in each input report, and then decodes
// reports into a normalised GamepadState. Pads differ (Xbox uses Z/Rz for the right stick,
// others Rx/Ry), so the right stick prefers Rx/Ry and falls back to Z/Rz.
#include <stddef.h>
#include <stdint.h>

struct GamepadState {
    float lx = 0, ly = 0, rx = 0, ry = 0;  // -1..1, +y = stick pushed down (HID convention)
    float lt = 0, rt = 0;                  // 0..1
    int hat = -1;                          // -1 centred, 0 = up, clockwise to 7
    uint32_t buttons = 0;                  // bit n-1 = HID button n
};

class HidGamepadParser {
public:
    // Returns the number of usable fields found (0 = not a gamepad descriptor we understand).
    int parse(const uint8_t *descriptor, size_t length);
    // Applies one input report. `reportId` is 0 when the device does not use report IDs.
    bool decode(uint8_t reportId, const uint8_t *data, size_t length, GamepadState &state) const;
    void clear() { count_ = 0; }

private:
    enum Kind : uint8_t { AXIS_X, AXIS_Y, AXIS_Z, AXIS_RX, AXIS_RY, AXIS_RZ, HAT, BUTTON, TRIGGER_L, TRIGGER_R };
    struct Field {
        uint8_t reportId;
        Kind kind;
        uint8_t button;     // 1-based button number for BUTTON
        uint16_t bitOffset;
        uint8_t bitSize;
        int32_t logicalMin, logicalMax;
    };
    static constexpr int kMaxFields = 48;
    void add(const Field &field);
    Field fields_[kMaxFields];
    int count_ = 0;
    bool hasRxRy_ = false;
};
