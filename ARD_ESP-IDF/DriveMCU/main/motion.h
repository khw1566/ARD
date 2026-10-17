#pragma once
// Motion scheduler: owns the stepper engine, the move queue, the inter-command delay
// and the rail PID. Runs as a fixed-rate 1 kHz task pinned to core 1.
#include <stdint.h>
#include "esp_err.h"

enum Axis { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2 };

struct MotionStatus {
    bool busy;
    bool stopped;
    bool pid;
    bool homing;
    int32_t x, y, z;
    float stepsPerMmZ;
    int targetRailDist;
    uint32_t lastMoveMs;
    uint32_t lastHomingMs;
    uint32_t queued;
};

esp_err_t motion_start();

// Each returns false when the request is rejected (queue full / lock timeout / invalid).
bool motion_queue(int32_t stepsX, int32_t stepsY, int32_t stepsZ, char axis); // axis: x y z b
void motion_emergency_stop();               // immediate halt, queue cleared, drivers disabled
bool motion_stop_axis(Axis axis);           // decelerating stop of one axis
bool motion_set_speed(Axis axis, int stepsPerSecond);
bool motion_set_accel(Axis axis, int stepsPerSecond2);
void motion_enable(Axis axis, bool enabled);
void motion_enable_all(bool enabled);
bool motion_home();                         // X/Y to 0 (open-loop Z keeps its position)
void motion_clear_stop();                   // a new move command re-arms motion
void motion_set_inter_command_delay(uint32_t ms);

// Rail PID (needs the ToF sensor).
void motion_set_pid_p(float p);
void motion_set_pid_i(float i);
void motion_set_pid_d(float d);
void motion_set_rail_target(int mm);
bool motion_set_pid_enabled(bool enabled); // false when enabling without a live ToF
bool motion_set_steps_per_mm(float factor);

bool motion_status(MotionStatus *out, uint32_t timeoutMs);
uint32_t motion_boot_id();
