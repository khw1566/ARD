#pragma once
// Timer-driven step generator for three A4988 axes.
//
// The PlatformIO firmware called AccelStepper::run() once per 1 ms tick, which capped
// every axis at ~1000 steps/s and made the step timing depend on task scheduling.
// Here a GPTimer interrupt (20 kHz, core 1) emits the pulses with a phase accumulator,
// and a 1 kHz planner task updates each axis' velocity with a trapezoidal profile.
// The interrupt stops exactly at the target position, so planner jitter never adds
// or loses steps.
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"

class StepperEngine {
public:
    static constexpr int kAxes = 3;
    static constexpr uint32_t kTickHz = 20000;      // ISR rate
    static constexpr float kMaxStepRate = 10000.0f; // one step needs a high and a low tick

    struct Pins {
        gpio_num_t step;
        gpio_num_t dir;
        bool invertDir;
    };

    esp_err_t begin(const Pins (&pins)[kAxes]);

    // Planner side: call from one task at a time (the motion task holds its mutex).
    void setMaxSpeed(int axis, float stepsPerSecond);
    void setAcceleration(int axis, float stepsPerSecond2);
    void move(int axis, int32_t relative);
    void moveTo(int axis, int32_t absolute);
    void stop(int axis);                       // decelerate to rest (AccelStepper::stop)
    void setCurrentPosition(int axis, int32_t position);
    void update(float dt);                     // velocity planner, run at ~1 kHz

    int32_t position(int axis) const;
    int32_t target(int axis) const;
    int32_t distanceToGo(int axis) const { return target(axis) - position(axis); }
    bool moving(int axis) const;
    bool anyMoving() const;
    float speed(int axis) const { return plan_[axis].speed; }
    float maxSpeed(int axis) const { return plan_[axis].maxSpeed; }
    float acceleration(int axis) const { return plan_[axis].accel; }

    // Immediate halt that does not need the planner: safe from any task.
    void haltAll();

private:
    struct Shared {
        volatile int32_t position;
        volatile int32_t target;
        volatile uint32_t rate;   // Q16 steps per tick
        volatile int8_t dir;      // +1 / -1
        int8_t appliedDir;
        bool pulseHigh;
        uint32_t phase;
        uint32_t stepMask;
        uint32_t dirMask;
        bool invertDir;
    };
    struct Plan {
        float speed = 0;          // signed steps/s
        float maxSpeed = 1000;
        float accel = 500;
    };

    static bool onAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *ctx);
    void updateAxis(int axis, float dt);

    Shared shared_[kAxes] = {};
    Plan plan_[kAxes];
    volatile uint32_t haltSeq_ = 0;
    uint32_t seenHaltSeq_ = 0;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    gptimer_handle_t timer_ = nullptr;
};
