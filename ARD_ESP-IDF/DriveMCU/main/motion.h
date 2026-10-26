#pragma once
// Motion scheduler: owns the stepper engine, the move queue, the inter-command delay,
// the rail PID and the continuous jog. Runs as a fixed-rate 1 kHz task pinned to core 1.
#include <stdint.h>
#include "esp_err.h"

enum Axis { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2 };

struct MotionStatus {
    bool busy;
    bool stopped;
    bool pid;
    bool homing;
    bool jogging;
    bool program;         // a queued move or homing owns the axes (jog is rejected)
    int32_t x, y, z;
    float stepsPerMmZ;
    float pidP, pidI, pidD;
    int targetRailDist;
    int railDir;          // +1: +Z steps lengthen the ToF distance, -1: shorten it
    bool railSettled;     // PID on, inside the 2 mm dead band and Z at rest
    // Z safety limits (ToF mm). While on, every Z motion stops before leaving the range.
    bool railLimitOn;
    int railMinMm, railMaxMm;
    uint32_t railLimitCount;   // safety stops/refusals since boot
    char railLimitReason[64];  // last one, "" when none
    // Disc tilt safety limit: tilt = (X - Y) / 2 steps x 1.8 deg / 4 (reducer). Rotation is free.
    float tiltDeg;
    int tiltLimitDeg;
    uint32_t tiltLimitCount;   // refused moves, jog holds and backstop halts since boot
    char tiltLimitReason[64];  // last one, "" when none
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
bool motion_home();                         // X/Y to 0 (Z keeps its position, PID off)
bool motion_set_origin();                   // current X/Y/Z position becomes 0; false while moving
void motion_clear_stop();                   // a new move command re-arms motion
void motion_set_inter_command_delay(uint32_t ms);

// Rail PID (needs the ToF sensor). The PID moves Z until the ToF distance equals the
// target. It turns itself off when the error grows 6 mm past its best value (wrong
// direction); the stop is logged.
bool motion_set_pid_p(float p);
bool motion_set_pid_i(float i);
bool motion_set_pid_d(float d);
bool motion_set_rail_target(int mm);        // false outside the safety limits
bool motion_set_pid_enabled(bool enabled); // false when enabling without a live ToF
bool motion_set_steps_per_mm(float factor);   // stored in NVS
bool motion_set_rail_dir(int dir);            // +1 / -1, stored in NVS; false while the PID runs
// Z safety limits in ToF mm (default 70..120, NVS). Off: bench tests without the ToF mounted.
// While on, Z motion (mzr, Z jog, PID) needs a fresh ToF and stops before leaving the range;
// moving back toward the range is always allowed.
bool motion_set_rail_limits(bool on, int minMm, int maxMm);
void motion_load_rail_settings();             // after nvs_flash_init

bool motion_status(MotionStatus *out, uint32_t timeoutMs);
uint32_t motion_boot_id();

// Continuous jog: per-motor velocity in -1..1 of the axis max speed (mxs/mys), ramped with
// the axis acceleration. Input must be refreshed within 400 ms, otherwise the axes decelerate
// to a stop (dead-man). All zero is a decelerating stop. Returns false while a queued move
// or homing owns the axes. A non-zero Z (rail) jog turns the rail PID off; X/Y jogs run
// alongside it.
bool motion_jog(float vx, float vy, float vz);
