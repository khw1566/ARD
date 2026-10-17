#pragma once
// BLE HID gamepad host. The Bluetooth controller is the ESP32-C6 (ESP-Hosted VHCI over SDIO);
// NimBLE and the HID host run on the P4.
//
// Pairing: with no saved pad, or after gamepad_pair() from the UI, the first advertising HID
// gamepad/joystick is bonded and remembered. Otherwise only the remembered pad is reconnected,
// so a stranger's controller nearby cannot take over the machine.
#include <stdint.h>
#include "esp_err.h"
#include "hid_gamepad_parser.h"

enum GamepadLink { GAMEPAD_OFF, GAMEPAD_SEARCHING, GAMEPAD_PAIRING, GAMEPAD_CONNECTING, GAMEPAD_CONNECTED };

struct GamepadInfo {
    GamepadLink link;
    char name[32];
    char address[18];
    uint32_t reports;       // input reports received since connect
    uint32_t ageMs;         // since the last input report
    int battery;            // %, -1 unknown
};

esp_err_t gamepad_start();
bool gamepad_state(GamepadState *out);   // false when no pad is connected
GamepadInfo gamepad_info();
void gamepad_pair();                     // accept a new pad for 60 s
void gamepad_forget();                   // drop the saved pad and its bond
