#pragma once
// Gamepad -> machine mapping (replaces the ControlMCU joystick + toggle switch).
//
// DRIVE mode (local motors, no PC round trip):
//   left stick  up/down  disk forward/back (X +, Y -)    left/right  disk rotate (X and Y same way)
//   right stick up/down  Z rail                           LT held     precision (25 % speed)
//   D-pad up/down        jog speed 10..100 %
// ROBOT mode (Robot3Axis through the PC, as ControlMCU mode A/N did):
//   left stick X/Y -> rx/ry steps, right stick Y -> rz steps, A -> gripper open/close
// Any mode:
//   B      EMERGENCY STOP (same as the "x" command)     START   re-arm after a stop
//   Y      switch DRIVE/ROBOT                            X (hold 1 s)  home X/Y
//   LB/RB  previous / next screen tab
// Button numbers are the HID ones of an Xbox Wireless pad in BLE mode; other pads may differ.
// The System tab shows the raw button numbers to adjust kButton* in controls.cpp.
#include <stdint.h>
#include "esp_err.h"

enum ControlMode { MODE_DRIVE, MODE_ROBOT };

struct ControlStatus {
    ControlMode mode;
    int speedPercent;       // jog speed scale
    bool precision;
    bool jogging;
    char lastAction[48];
};

esp_err_t controls_start();
ControlStatus controls_status();
void controls_set_mode(ControlMode mode);
void controls_set_speed(int percent);

// The UI registers this to follow LB/RB tab switching (called from the controls task).
void controls_on_tab_step(void (*callback)(int direction));
