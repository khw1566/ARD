#include "controls.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "buzzer.h"
#include "commands.h"
#include "gamepad.h"
#include "motion.h"
#include "pc_link.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

// HID button numbers (1-based). Xbox Wireless Controller over BLE.
constexpr int kButtonA = 1, kButtonB = 2, kButtonX = 4, kButtonY = 5, kButtonLB = 7, kButtonRB = 8, kButtonStart = 12;
constexpr float kDeadzone = 0.12f;
constexpr uint32_t kPeriodMs = 20;         // 50 Hz: well inside the 300 ms jog dead-man timeout
constexpr uint32_t kHomeHoldMs = 1000;
constexpr uint32_t kRobotRepeatMs = 200;

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
ControlStatus status = {MODE_DRIVE, 50, false, false, "Ready"};
void (*tabStep)(int) = nullptr;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

bool pressed(uint32_t buttons, int n) { return buttons & (1u << (n - 1)); }

// Dead zone with rescale, then a square curve for fine control near centre.
float shape(float v) {
    const float a = fabsf(v);
    if (a < kDeadzone) return 0;
    const float t = (a - kDeadzone) / (1 - kDeadzone);
    return copysignf(t * t, v);
}

void note(const char *format, const char *arg = "") {
    portENTER_CRITICAL(&lock);
    snprintf(status.lastAction, sizeof(status.lastAction), format, arg);
    portEXIT_CRITICAL(&lock);
}

void driveJog(const GamepadState &s, int speed, bool precision) {
    const float forward = -shape(s.ly), rotate = shape(s.lx), rail = -shape(s.ry);
    float x = forward + rotate, y = -forward + rotate;  // bevel differential (df: X+,Y-  dr: X+,Y+)
    const float peak = fmaxf(fabsf(x), fabsf(y));
    if (peak > 1) { x /= peak; y /= peak; }
    const float scale = speed / 100.0f * (precision ? 0.25f : 1.0f);
    const bool active = x != 0 || y != 0 || rail != 0;
    const bool ok = motion_jog(x * scale, y * scale, rail * scale);
    portENTER_CRITICAL(&lock);
    status.jogging = active && ok;
    portEXIT_CRITICAL(&lock);
}

void robotJog(const GamepadState &s, uint32_t now) {
    static uint32_t last = 0;
    if (now - last < kRobotRepeatMs) return;
    const float axes[3] = {shape(s.lx), -shape(s.ly), -shape(s.ry)};
    for (int i = 0; i < 3; ++i) {
        if (fabsf(axes[i]) < 0.25f) continue;
        const int step = fabsf(axes[i]) > 0.8f ? 50 : 10;
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "r%c%s%d", "xyz"[i], axes[i] < 0 ? "-" : "", step);
        pc_send_command(PC_ROBOT, cmd); // same command text as the HMI Robot page
        last = now;
    }
}

void controlTask(void *) {
    uint32_t previous = 0, homeSince = 0;
    int lastHat = -1;
    bool wasConnected = false, gripperOpen = false;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
        GamepadState s;
        const bool connected = gamepad_state(&s);
        if (!connected) {
            if (wasConnected) { motion_jog(0, 0, 0); note("Gamepad lost: jog stopped"); buzzer_play(MELODY_CONNECTION_LOST); }
            wasConnected = false;
            previous = 0;
            portENTER_CRITICAL(&lock);
            status.jogging = false;
            portEXIT_CRITICAL(&lock);
            continue;
        }
        wasConnected = true;
        const uint32_t now = nowMs(), b = s.buttons, edge = b & ~previous;
        previous = b;

        if (edge & (1u << (kButtonB - 1))) { command_execute("stop"); note("E-STOP (pad B)"); continue; }
        if (edge & (1u << (kButtonStart - 1))) { motion_clear_stop(); note("Re-armed (pad START)"); }
        if (edge & (1u << (kButtonY - 1))) {
            controls_set_mode(status.mode == MODE_DRIVE ? MODE_ROBOT : MODE_DRIVE);
            motion_jog(0, 0, 0);
        }
        if (tabStep && (edge & (1u << (kButtonLB - 1)))) tabStep(-1);
        if (tabStep && (edge & (1u << (kButtonRB - 1)))) tabStep(+1);
        // Homing moves both disk motors: require a deliberate 1 s hold.
        if (pressed(b, kButtonX)) {
            if (!homeSince) homeSince = now;
            else if (homeSince != 1 && now - homeSince >= kHomeHoldMs) {
                if (status.mode == MODE_DRIVE) command_execute("home");
                else pc_send_command(PC_ROBOT, "home");
                note("Home (pad X held)");
                homeSince = 1; // fire once per hold
            }
        } else {
            homeSince = 0;
        }
        if (s.hat != lastHat && (s.hat == 0 || s.hat == 4)) {
            controls_set_speed(status.speedPercent + (s.hat == 0 ? 10 : -10));
        }
        lastHat = s.hat;

        portENTER_CRITICAL(&lock);
        const ControlMode mode = status.mode;
        const int speed = status.speedPercent;
        status.precision = s.lt > 0.5f;
        portEXIT_CRITICAL(&lock);
        if (mode == MODE_DRIVE) {
            driveJog(s, speed, s.lt > 0.5f);
        } else {
            motion_jog(0, 0, 0);
            if (edge & (1u << (kButtonA - 1))) {
                gripperOpen = !gripperOpen;
                pc_send_command(PC_ROBOT, gripperOpen ? "grab open" : "grab close");
                note(gripperOpen ? "Gripper open" : "Gripper close");
            }
            robotJog(s, now);
        }
    }
}

} // namespace

esp_err_t controls_start() {
    return xTaskCreatePinnedToCore(controlTask, "controls", 4096, nullptr, 7, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

ControlStatus controls_status() {
    portENTER_CRITICAL(&lock);
    ControlStatus copy = status;
    portEXIT_CRITICAL(&lock);
    return copy;
}

void controls_set_mode(ControlMode mode) {
    portENTER_CRITICAL(&lock);
    status.mode = mode;
    portEXIT_CRITICAL(&lock);
    note(mode == MODE_DRIVE ? "Mode: DRIVE%s" : "Mode: ROBOT%s");
}

void controls_set_speed(int percent) {
    percent = percent < 10 ? 10 : percent > 100 ? 100 : percent;
    portENTER_CRITICAL(&lock);
    status.speedPercent = percent;
    portEXIT_CRITICAL(&lock);
}

void controls_on_tab_step(void (*callback)(int direction)) { tabStep = callback; }
