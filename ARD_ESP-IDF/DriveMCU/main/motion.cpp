#include "motion.h"
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "stepper_engine.h"
#include "sensors.h"
#include "telemetry.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "motion";

namespace {

struct MotorAction {
    int32_t steps[3];
    uint32_t generation;
    char axis; // 'x', 'y', 'z', 'b' (x and y together)
};

constexpr gpio_num_t kEnablePins[3] = {PIN_EN_X, PIN_EN_Y, PIN_EN_Z};
constexpr TickType_t kLockTimeout = pdMS_TO_TICKS(100);
constexpr uint32_t kJogTimeoutMs = 400;   // jog input must be refreshed faster than this
constexpr int32_t kJogLead = 20000;       // steps of target kept ahead of a jogging axis
constexpr uint32_t kPidPeriodMs = 50;
constexpr uint32_t kTofMaxAgeMs = 200;
constexpr float kRailDeadBandMm = 2.0f;
constexpr float kRailHoldEnterMm = 1.5f; // resume only after two fresh samples outside +/-2 mm
// The PID gives up when the error grows this far past its best value since the last
// enable or target change: a wrong rail direction (mzv) runs away from the target.
constexpr float kRailRunawayMm = 6.0f; // no false trips in 840 simulated moves (2 mm ToF noise)
// Z safety limit guard. Stop margin toward a limit = braking distance + ToF lag travel + 1 mm.
// A PID target may sit exactly on a limit (sequence origin 120 mm), so the guard allows the
// PID dead band past it before stopping.
constexpr float kLimitTofLagS = 0.3f;
constexpr float kLimitExtraMm = 1.0f;
constexpr float kLimitToleranceMm = 2.0f;
// Disc tilt safety limit (user spec 2026-10-27). The disc is a differential pair behind 4:1
// reducers at 1.8 deg/step: tilt = (X - Y) / 2 motor steps, rotation = (X + Y) / 2 (PC
// tilt_steps: forward = +X / -Y). Only the tilt is limited; rotation stays free.
constexpr float kDiskDegPerStep = 1.8f / ARD_DISK_GEAR_RATIO;
constexpr int32_t kTiltLimit = (int32_t)(2.0f * ARD_DISK_TILT_LIMIT_DEG / kDiskDegPerStep); // on X - Y
constexpr int32_t kTiltTolerance = 2; // the jog wall sits this far inside, the backstop at the limit

StepperEngine engine;
SemaphoreHandle_t mutex;
QueueHandle_t queue;

// All fields below are protected by `mutex` unless they are atomics.
uint32_t generation = 0;
uint32_t interCommandDelay = 0;
bool waitingForDelay = false;
MotorAction pending = {};
uint32_t delayStart = 0;
bool executing = false;
uint32_t moveStart = 0;
bool homing = false;
uint32_t homingStart = 0;
std::atomic<uint32_t> lastMoveMs{0}, lastHomingMs{0};
std::atomic<bool> stopActive{false};
std::atomic<bool> pidEnabled{false};
uint32_t bootId = 0;

int targetRailDist = 120;
float stepsPerMmZ = 43.48f;
float pidP = 0.12f, pidI = 0.0f, pidD = 0.0f;
float integralRail = 0, lastRailError = 0;
uint32_t lastPid = 0;
// Sign between Z steps and the ToF distance. +1 was the original rail (+steps lengthen the
// distance). The rail is mounted mirrored since 2026-10-26, so +steps shorten it: -1.
// Measured and stored by the PC rail tool (mzv), kept in NVS.
int railDir = -1;
float bestRailError = INFINITY;
bool railSettled = false;
bool railHolding = false, railCorrectionPending = false;
uint8_t railReleaseCount = 0;
uint32_t railSampleStamp = 0, railRestSince = 0;
float railDerivative = 0;
bool railLimitOn = true;
int railMinMm = 70, railMaxMm = 120;
uint32_t railLimitCount = 0;
char railLimitReason[64] = "";
bool railLimitStopped = false; // one stop (and log) per event
int railStopSign = 0;           // step direction that was stopped
int32_t railStopTarget = 0;     // braking target held while that event lasts
uint32_t railNoTofLogMs = 0;
int32_t queuedSteps[2] = {0, 0}; // X/Y steps of actions in the queue or waiting for the delay
uint32_t tiltLimitCount = 0;
char tiltLimitReason[64] = "";
bool tiltBackstopTripped = false; // one halt (and log) per event
bool tiltJogAtLimit = false;

float tiltDeg(int32_t x, int32_t y) { return (float)(x - y) * 0.5f * kDiskDegPerStep; }

void tiltLimitEvent(const char *reason) {
    strlcpy(tiltLimitReason, reason, sizeof(tiltLimitReason));
    ++tiltLimitCount;
    log_async("Disc tilt limit: %s (limit +-%d deg)", reason, ARD_DISK_TILT_LIMIT_DEG);
}

void railLimitEvent(const char *reason) {
    // A jog held against a limit repeats the event; log a repeat at most every 2 s.
    static uint32_t lastLog = 0;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const bool repeat = !strcmp(railLimitReason, reason) && now - lastLog < 2000;
    strlcpy(railLimitReason, reason, sizeof(railLimitReason));
    ++railLimitCount;
    if (repeat) return;
    lastLog = now;
    log_async("Z safety limit: %s (limit %d..%d mm)", reason, railMinMm, railMaxMm);
}

bool railTofFresh(float *distance) {
    uint32_t age;
    return sensors_tof(distance, &age) && age <= kTofMaxAgeMs && *distance > 0 && *distance < 2000;
}

// While limits are on, Z needs a fresh ToF. Logs at most once a second.
bool railMotionAllowed(uint32_t now) {
    float distance;
    if (!railLimitOn || railTofFresh(&distance)) return true;
    if (now - railNoTofLogMs >= 1000) {
        railNoTofLogMs = now;
        railLimitEvent("no fresh ToF: Z refused (check the sensor or mzl0 = limits off)");
    }
    return false;
}

void resetRailFeedback() {
    integralRail = lastRailError = railDerivative = 0;
    railSampleStamp = railRestSince = 0;
    railHolding = railCorrectionPending = railSettled = false;
    railReleaseCount = 0;
}

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

struct Lock {
    bool held;
    explicit Lock(TickType_t timeout = kLockTimeout) : held(xSemaphoreTake(mutex, timeout) == pdTRUE) {}
    ~Lock() { if (held) xSemaphoreGive(mutex); }
};

void setEnable(int axis, bool enabled) { gpio_set_level(kEnablePins[axis], enabled ? 0 : 1); }

// Continuous jog: per-motor velocity in -1..1 of the configured max speed.
float jogInput[3] = {0, 0, 0};
uint32_t jogStamp = 0;
bool jogging[3] = {false, false, false};
int configuredSpeed[3] = {500, 500, 500};

bool jogAny() { return jogging[0] || jogging[1] || jogging[2]; }

// A disc move may end inside the tilt limit, or closer to it on the same side (back toward level).
bool tiltMoveAllowed(int32_t x, int32_t y, int32_t dx, int32_t dy) {
    const int64_t before = (int64_t)x - y, after = before + dx - dy;
    return llabs(after) <= kTiltLimit || (after * before > 0 && llabs(after) < llabs(before));
}

// Drops the queued program: the moves after a refused one assume it happened.
void clearProgram() {
    ++generation;
    xQueueReset(queue);
    queuedSteps[0] = queuedSteps[1] = 0;
    waitingForDelay = false;
}

void startAction(const MotorAction &a, uint32_t now) {
    const int32_t dx = a.axis == 'z' ? 0 : a.steps[0], dy = a.axis == 'z' ? 0 : a.steps[1];
    queuedSteps[0] -= dx;
    queuedSteps[1] -= dy;
    const int32_t x = engine.position(AXIS_X), y = engine.position(AXIS_Y);
    if (!tiltMoveAllowed(x, y, dx, dy)) {
        clearProgram();
        char reason[64];
        snprintf(reason, sizeof(reason), "move to %+.1f deg refused, queue cleared", tiltDeg(x + dx, y + dy));
        tiltLimitEvent(reason);
        return;
    }
    executing = true;
    moveStart = now;
    for (int i = 0; i < 3; ++i) {
        const bool selected = (a.axis == 'b' && i < 2) || a.axis == "xyz"[i];
        if (!selected) continue;
        setEnable(i, true);
        engine.move(i, a.steps[i]);
    }
}

// Ported from the PlatformIO rail controller; uses only fresh ToF samples.
void railPid(uint32_t now) {
    static bool feedbackLost = true;
    if (!pidEnabled || stopActive) { lastRailError = 0; integralRail = 0; railSettled = false; feedbackLost = true; return; }
    if (now - lastPid < kPidPeriodMs) return;
    lastPid = now;
    float distance;
    uint32_t age, sampleStamp;
    if (!sensors_tof(&distance, &age, &sampleStamp) || age > kTofMaxAgeMs) {
        if (engine.distanceToGo(AXIS_Z) != 0) engine.stop(AXIS_Z); // no feedback: hold the rail
        resetRailFeedback();
        feedbackLost = true;
        return;
    }
    if (sampleStamp == railSampleStamp) return; // never integrate/differentiate one sample twice
    const bool firstSample = railSampleStamp == 0;
    const float dt = railSampleStamp ? fminf((sampleStamp - railSampleStamp) / 1000.0f, 0.2f) : 0.05f;
    railSampleStamp = sampleStamp;
    railSettled = false;
    if (distance <= 0 || distance >= 2000) return;
    const float error = (float)targetRailDist - distance;
    if (feedbackLost || firstSample) { lastRailError = error; railDerivative = 0; feedbackLost = false; }
    const float absError = fabsf(error);
    if (absError < bestRailError) {
        bestRailError = absError;
    } else if (absError > bestRailError + kRailRunawayMm) {
        pidEnabled = false;
        engine.stop(AXIS_Z);
        integralRail = 0;
        log_async("Rail PID OFF: error grew %.1f -> %.1f mm. Check rail direction (now mzv%d)",
                  bestRailError, absError, railDir);
        return;
    }
    if (absError <= kRailHoldEnterMm) { railHolding = true; railReleaseCount = 0; }
    if (railHolding) {
        if (absError > kRailDeadBandMm) ++railReleaseCount;
        else railReleaseCount = 0;
        if (railReleaseCount >= 2) { railHolding = false; feedbackLost = true; }
    }
    if (railHolding) {
        if (engine.distanceToGo(AXIS_Z) != 0) engine.stop(AXIS_Z);
        integralRail = 0;
        lastRailError = error;
        railSettled = absError <= kRailDeadBandMm && !engine.moving(AXIS_Z);
        railDerivative = 0;
        railCorrectionPending = false;
        return;
    }
    // Close to the target, finish a short correction and let the 3-frame ToF filter
    // observe the stopped rail before issuing another correction.
    if (absError < 8.0f && railCorrectionPending) {
        if (engine.moving(AXIS_Z)) return;
        if (!railRestSince) railRestSince = now;
        if (now - railRestSince < 120) return;
        railCorrectionPending = false;
        railRestSince = 0;
        feedbackLost = true;
    }
    if (feedbackLost) { lastRailError = error; railDerivative = 0; feedbackLost = false; }
    float p = pidP;
    if (fabsf(error) < 3.0f) p *= 0.5f; // soft landing
    const float pTerm = error * stepsPerMmZ * p;
    if (fabsf(error) < 10.0f) integralRail = fminf(fmaxf(integralRail + error * dt, -50.0f), 50.0f);
    else integralRail = 0;
    const float iTerm = integralRail * stepsPerMmZ * pidI;
    railDerivative = 0.75f * railDerivative + 0.25f * (error - lastRailError) / dt;
    const float dTerm = railDerivative * stepsPerMmZ * pidD * 0.01f;
    lastRailError = error;
    const float limit = fmaxf(1.0f, fminf(2.0f, absError * 0.25f) * stepsPerMmZ);
    const float output = pTerm + iTerm + dTerm;
    if (fabsf(output) > limit) integralRail = 0; // no stored integral during saturation
    const int32_t delta = (int32_t)lroundf(railDir * fminf(fmaxf(output, -limit), limit));
    if (delta) {
        setEnable(AXIS_Z, true);
        engine.moveTo(AXIS_Z, engine.position(AXIS_Z) + delta);
        railCorrectionPending = true;
        railRestSince = 0;
    }
}

// Where an axis comes to rest if it brakes now with its acceleration.
int32_t stopPosition(int axis) {
    const float v = engine.speed(axis);
    const int32_t brake = (int32_t)ceilf(v * v / (2.0f * engine.acceleration(axis)));
    return engine.position(axis) + (v > 0 ? brake : v < 0 ? -brake : 0);
}

// Keeps a disc axis target within the jog tilt wall against where the other axis will stop:
// X - Y in [-W, W] gives the same [other - W, other + W] range for X and for Y. Using the
// other axis' stopping point (not its position) leaves room when both axes tilt apart. The
// target is never put behind the axis, so a jog cannot run backwards when already outside.
// A Python model of these rules (30 random stick runs) peaked at 266 steps = 59.85 deg.
int32_t tiltClampedTarget(int axis, int32_t target) {
    constexpr int32_t kWall = kTiltLimit - kTiltTolerance;
    const int32_t pos = engine.position(axis), other = stopPosition(1 - axis);
    if (target > pos) return std::max(pos, std::min(target, other + kWall));
    if (target < pos) return std::min(pos, std::max(target, other - kWall));
    return target;
}

void jogTick(uint32_t now) {
    // Signed: motion_jog may stamp a time later than the `now` sampled before the lock.
    const bool fresh = (int32_t)(now - jogStamp) <= (int32_t)kJogTimeoutMs;
    bool atLimit = false;
    for (int i = 0; i < 3; ++i) {
        const float v = fresh && !stopActive ? jogInput[i] : 0.0f;
        if (v != 0.0f) {
            jogging[i] = true;
            setEnable(i, true);
            // Never faster than the axis max speed (see motion_start).
            engine.setMaxSpeed(i, fabsf(v) * configuredSpeed[i]);
            int32_t target = engine.position(i) + (v > 0 ? kJogLead : -kJogLead);
            if (i != AXIS_Z) {
                // Stick reversed: brake to a clamped stop first. The planner would otherwise
                // keep running the old way for v^2/2a steps, past the wall.
                const float speed = engine.speed(i);
                if (speed != 0.0f && (speed > 0) != (v > 0)) target = stopPosition(i);
                target = tiltClampedTarget(i, target); // disc: soft wall at the tilt limit
                atLimit = atLimit || (target == engine.position(i) && speed == 0.0f);
            }
            engine.moveTo(i, target);
        } else if (jogging[i]) {
            jogging[i] = false;
            engine.stop(i); // decelerate with the axis acceleration
            if (i != AXIS_Z) engine.moveTo(i, tiltClampedTarget(i, engine.target(i)));
            engine.setMaxSpeed(i, configuredSpeed[i]);
        }
    }
    // The other axis keeps moving, so a decelerating disc axis is re-clamped every tick too.
    // A queued program or homing was validated as a whole and is left alone.
    if (!executing && !homing && !waitingForDelay) {
        for (int i = AXIS_X; i <= AXIS_Y; ++i) {
            const int32_t target = engine.target(i);
            if (target != engine.position(i)) {
                const int32_t clamped = tiltClampedTarget(i, target);
                if (clamped != target) engine.moveTo(i, clamped);
            }
        }
    }
    if (atLimit && !tiltJogAtLimit) {
        char reason[64];
        snprintf(reason, sizeof(reason), "jog held at %+.1f deg",
                 tiltDeg(engine.position(AXIS_X), engine.position(AXIS_Y)));
        tiltLimitEvent(reason);
    }
    tiltJogAtLimit = atLimit;
}

// Backstop for every X/Y motion source (queue, jog, homing): once the tilt is past the limit
// and still growing, X and Y halt at once. A decelerating stop at 300 steps/s would add about
// 17 deg. A running program is dropped, since its later moves assume this one finished.
void diskTiltGuard() {
    const int32_t tilt = engine.position(AXIS_X) - engine.position(AXIS_Y);
    const float rate = engine.speed(AXIS_X) - engine.speed(AXIS_Y);
    const bool outward = (tilt > 0 && rate > 0) || (tilt < 0 && rate < 0);
    const bool past = tilt > kTiltLimit || tilt < -kTiltLimit;
    if (!past || !outward) { tiltBackstopTripped = false; return; }
    if (tiltBackstopTripped) return;
    tiltBackstopTripped = true;
    for (int i = AXIS_X; i <= AXIS_Y; ++i) {
        jogInput[i] = 0;
        engine.setCurrentPosition(i, engine.position(i));
    }
    if (executing || waitingForDelay || uxQueueMessagesWaiting(queue) > 0) clearProgram();
    homing = false;
    char reason[64];
    snprintf(reason, sizeof(reason), "disc halted at %+.1f deg", tiltDeg(engine.position(AXIS_X), engine.position(AXIS_Y)));
    tiltLimitEvent(reason);
}

// Z safety limit guard, every 1 ms tick and for every Z motion source (queue, jog, PID).
// d(ToF)/dt = railDir * v / stepsPerMm. A motion toward a limit stops with the Z
// acceleration once the remaining distance is within the stop margin; past the limit it
// stops at once. A motion away from a limit is never stopped.
// engine.stop() is issued once per event and the braking target is then held: calling it
// every tick would push the target ahead each time and the axis would never slow down.
void railLimitGuard() {
    const float v = engine.speed(AXIS_Z);
    const int32_t togo = engine.distanceToGo(AXIS_Z);
    if (!railLimitOn || (v == 0.0f && togo == 0)) { railLimitStopped = false; return; }
    const int stepSign = v > 0 ? 1 : v < 0 ? -1 : (togo > 0 ? 1 : -1);
    const int tofSign = railDir * stepSign; // +1: the distance grows (toward max)
    if (railLimitStopped && stepSign != railStopSign) railLimitStopped = false; // moving back
    if (railLimitStopped) {
        // Hold the braking target against new targets in the same direction (jog lead, PID).
        const int32_t target = engine.target(AXIS_Z);
        if (stepSign > 0 ? target > railStopTarget : target < railStopTarget) engine.moveTo(AXIS_Z, railStopTarget);
        jogInput[AXIS_Z] = 0;
        return;
    }
    float distance;
    const bool fresh = railTofFresh(&distance);
    char reason[64];
    if (fresh) {
        const float speedMm = fabsf(v) / stepsPerMmZ;
        const float brakeMm = v * v / (2.0f * engine.acceleration(AXIS_Z)) / stepsPerMmZ;
        const float margin = brakeMm + kLimitTofLagS * speedMm + kLimitExtraMm;
        const float limit = tofSign > 0 ? railMaxMm + kLimitToleranceMm : railMinMm - kLimitToleranceMm;
        const float remaining = tofSign > 0 ? limit - distance : distance - limit;
        if (remaining > margin) return;
        if (remaining <= 0) engine.setCurrentPosition(AXIS_Z, engine.position(AXIS_Z)); // already past: stop now
        else engine.stop(AXIS_Z); // decelerate with the Z acceleration
        snprintf(reason, sizeof(reason), "Z stopped at ToF %.1f mm near the %s limit", distance, tofSign > 0 ? "max" : "min");
    } else {
        engine.stop(AXIS_Z);
        strlcpy(reason, "no fresh ToF: Z stopped (check the sensor or mzl0 = limits off)", sizeof(reason));
    }
    jogInput[AXIS_Z] = 0;
    railLimitStopped = true;
    railStopSign = stepSign;
    railStopTarget = engine.target(AXIS_Z);
    railLimitEvent(reason);
}

void scheduleTick(uint32_t now, float dt) {
    if (!engine.anyMoving()) {
        if (executing) {
            executing = false;
            lastMoveMs = now - moveStart;
            log_async("Motion complete in %lums", (unsigned long)lastMoveMs.load());
        }
        if (homing) {
            homing = false;
            lastHomingMs = now - homingStart;
            log_async("Homing complete in %lums", (unsigned long)lastHomingMs.load());
        }
        if (stopActive) waitingForDelay = false;
        if (!waitingForDelay && !stopActive && !jogAny()) {
            MotorAction next;
            if (xQueueReceive(queue, &next, 0) == pdTRUE && next.generation == generation) {
                if (interCommandDelay > 0) {
                    pending = next;
                    waitingForDelay = true;
                    delayStart = now;
                } else {
                    startAction(next, now);
                }
            }
        } else if (waitingForDelay && (now - delayStart >= interCommandDelay || stopActive)) {
            waitingForDelay = false;
            if (!stopActive && pending.generation == generation) startAction(pending, now);
        }
    }
    jogTick(now);
    diskTiltGuard();
    railPid(now);
    railLimitGuard();
    engine.update(dt);
}

void motionTask(void *) {
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    TickType_t wake = xTaskGetTickCount();
    int64_t last = esp_timer_get_time();
    for (;;) {
        vTaskDelayUntil(&wake, 1);
        esp_task_wdt_reset();
        const int64_t t = esp_timer_get_time();
        // Clamp so a long stall cannot produce one huge velocity step.
        const float dt = fminf((t - last) / 1e6f, 0.005f);
        last = t;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            scheduleTick((uint32_t)(t / 1000), dt);
            xSemaphoreGive(mutex);
        }
    }
}

} // namespace

esp_err_t motion_start() {
    bootId = esp_random();
    for (int i = 0; i < 3; ++i) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << kEnablePins[i];
        io.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&io));
        setEnable(i, false);
    }
    mutex = xSemaphoreCreateMutex();
    queue = xQueueCreate(32, sizeof(MotorAction));
    if (!mutex || !queue) return ESP_ERR_NO_MEM;

    static const StepperEngine::Pins pins[3] = {
        {PIN_STEP_X, PIN_DIR_X, false},
        {PIN_STEP_Y, PIN_DIR_Y, false},
        {PIN_STEP_Z, PIN_DIR_Z, true}, // rail direction inverted, as before
    };
    // The PlatformIO firmware was set to 1000 steps/s, but AccelStepper::run() in a 1 ms loop
    // quantized every interval below 2 ms to 2 ms, so the motors never saw more than ~500 steps/s.
    // Faster rates stall the loaded axes, which then slip backwards. The disc pair is slower
    // still (300 steps/s, 300 steps/s^2 by default) against overshoot and lost origin.
    const int speed[3] = {ARD_DISK_MAX_SPEED, ARD_DISK_MAX_SPEED, 500};
    const int accel[3] = {ARD_DISK_ACCEL, ARD_DISK_ACCEL, 1000};
    for (int i = 0; i < 3; ++i) {
        configuredSpeed[i] = speed[i];
        engine.setMaxSpeed(i, speed[i]);
        engine.setAcceleration(i, accel[i]);
    }

    // The task creates the timer so its interrupt is allocated on core 1 too.
    static TaskHandle_t creator;
    static esp_err_t result;
    creator = xTaskGetCurrentTaskHandle();
    auto boot = [](void *) {
        result = engine.begin(pins);
        xTaskNotifyGive(creator);
        if (result == ESP_OK) motionTask(nullptr);
        vTaskDelete(nullptr);
    };
    if (xTaskCreatePinnedToCore(boot, "motion", 4096, nullptr, configMAX_PRIORITIES - 3, nullptr, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (result != ESP_OK) ESP_LOGE(TAG, "step timer failed: %s", esp_err_to_name(result));
    return result;
}

bool motion_queue(int32_t x, int32_t y, int32_t z, char axis) {
    Lock lock;
    if (!lock.held) return false;
    if (z != 0 && axis == 'z' && !railMotionAllowed(nowMs())) return false;
    const int32_t dx = axis == 'z' ? 0 : x, dy = axis == 'z' ? 0 : y;
    // Disc tilt where this move would end: after the running move and the queued ones.
    // A jogging axis' target is its jog lead, so its position counts instead.
    const int32_t px = (jogging[AXIS_X] ? engine.position(AXIS_X) : engine.target(AXIS_X)) + queuedSteps[0];
    const int32_t py = (jogging[AXIS_Y] ? engine.position(AXIS_Y) : engine.target(AXIS_Y)) + queuedSteps[1];
    if (!tiltMoveAllowed(px, py, dx, dy)) {
        char reason[64];
        snprintf(reason, sizeof(reason), "move to %+.1f deg refused", tiltDeg(px + dx, py + dy));
        tiltLimitEvent(reason);
        return false;
    }
    MotorAction a = {{x, y, z}, generation, axis};
    if (xQueueSend(queue, &a, 0) != pdPASS) {
        log_async("Command Queue FULL! Action rejected.");
        return false;
    }
    queuedSteps[0] += dx;
    queuedSteps[1] += dy;
    return true;
}

void motion_emergency_stop() {
    // Stop pulses and drivers first so STOP never waits behind the planner lock.
    engine.haltAll();
    for (int i = 0; i < 3; ++i) setEnable(i, false);
    stopActive = true;
    pidEnabled = false;
    Lock lock(portMAX_DELAY);
    clearProgram(); // a new generation also invalidates an action already taken from the queue
    executing = false;
    homing = false;
    for (int i = 0; i < 3; ++i) {
        jogInput[i] = 0;
        if (jogging[i]) { jogging[i] = false; engine.setMaxSpeed(i, configuredSpeed[i]); }
        engine.setCurrentPosition(i, engine.position(i));
        setEnable(i, false); // again: a tick racing the halt may have just enabled a driver
    }
}

bool motion_stop_axis(Axis axis) {
    Lock lock;
    if (!lock.held) return false;
    engine.stop(axis);
    return true;
}

bool motion_set_speed(Axis axis, int v) {
    Lock lock;
    if (!lock.held) return false;
    const int cap = axis == AXIS_Z ? (int)StepperEngine::kMaxStepRate : ARD_DISK_MAX_SPEED;
    if (v > cap) {
        log_async("Speed %d limited to %d steps/s", v, cap);
        v = cap;
    }
    configuredSpeed[axis] = v;
    if (!jogging[axis]) engine.setMaxSpeed(axis, v);
    return true;
}

bool motion_set_accel(Axis axis, int a) {
    Lock lock;
    if (!lock.held) return false;
    engine.setAcceleration(axis, a);
    return true;
}

void motion_enable(Axis axis, bool enabled) { setEnable(axis, enabled); }
void motion_enable_all(bool enabled) { for (int i = 0; i < 3; ++i) setEnable(i, enabled); }

bool motion_home() {
    Lock lock(portMAX_DELAY);
    clearProgram();
    stopActive = false;
    pidEnabled = false;
    for (int i = 0; i < 3; ++i) { // homing takes over from a jog without the jog's stop target
        jogInput[i] = 0;
        if (jogging[i]) { jogging[i] = false; engine.setMaxSpeed(i, configuredSpeed[i]); }
    }
    setEnable(AXIS_X, true);
    setEnable(AXIS_Y, true);
    engine.moveTo(AXIS_X, 0);
    engine.moveTo(AXIS_Y, 0);
    homing = true;
    homingStart = nowMs();
    return true;
}

bool motion_set_origin() {
    Lock lock;
    if (!lock.held) return false;
    // Only at rest: a moving axis would land on a shifted target.
    if (executing || homing || waitingForDelay || jogAny() || engine.anyMoving() || uxQueueMessagesWaiting(queue) > 0)
        return false;
    for (int i = 0; i < 3; ++i) engine.setCurrentPosition(i, 0);
    return true;
}

void motion_clear_stop() { stopActive = false; }

void motion_set_inter_command_delay(uint32_t ms) {
    Lock lock(portMAX_DELAY);
    interCommandDelay = ms;
}

namespace {
bool setPidGain(float &gain, float value) {
    if (!isfinite(value) || value < 0 || value > 10000) return false;
    Lock lock(portMAX_DELAY);
    if (!lock.held || pidEnabled) return false;
    gain = value;
    resetRailFeedback();
    return true;
}
}
bool motion_set_pid_p(float p) { return setPidGain(pidP, p); }
bool motion_set_pid_i(float i) { return setPidGain(pidI, i); }
bool motion_set_pid_d(float d) { return setPidGain(pidD, d); }
bool motion_set_rail_target(int mm) {
    Lock lock(portMAX_DELAY);
    if (railLimitOn && (mm < railMinMm || mm > railMaxMm)) {
        char reason[64];
        snprintf(reason, sizeof(reason), "PID target %d mm refused (outside)", mm);
        railLimitEvent(reason);
        return false;
    }
    targetRailDist = mm;
    resetRailFeedback();
    bestRailError = INFINITY; // a new target may start farther away
    railSettled = false;
    return true;
}

bool motion_set_pid_enabled(bool enabled) {
    // Rail PID needs ARD_ENABLE_RAIL_PID in menuconfig and a working ToF.
    float distance;
    uint32_t age;
    if (enabled && (!ARD_RAIL_PID_ENABLED || !sensors_tof_ready() || !sensors_tof(&distance, &age))) return false;
    Lock lock(portMAX_DELAY);
    if (enabled && railLimitOn && (targetRailDist < railMinMm || targetRailDist > railMaxMm)) {
        railLimitEvent("PID refused: target outside the limits, set mzt first");
        return false;
    }
    if (enabled) {
        // The PID re-targets Z every 50 ms; a Z jog would fight it. Disk jogs continue.
        jogInput[AXIS_Z] = 0;
        if (jogging[AXIS_Z]) {
            jogging[AXIS_Z] = false;
            engine.stop(AXIS_Z);
            engine.setMaxSpeed(AXIS_Z, configuredSpeed[AXIS_Z]);
        }
        resetRailFeedback();
        bestRailError = INFINITY;
        lastPid = nowMs();
    } else if (pidEnabled) {
        engine.stop(AXIS_Z); // no PID target left behind
    }
    railSettled = false;
    pidEnabled = enabled;
    return true;
}

namespace {
void saveRailSettings(int dir, float stepsPerMm) {
    nvs_handle_t nvs;
    if (nvs_open("ard-rail", NVS_READWRITE, &nvs) != ESP_OK) return;
    uint32_t bits;
    memcpy(&bits, &stepsPerMm, sizeof(bits));
    const bool ok = nvs_set_i8(nvs, "dir", (int8_t)dir) == ESP_OK && nvs_set_u32(nvs, "spm", bits) == ESP_OK &&
                    nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    if (!ok) log_async("Rail settings not saved to NVS");
}

void saveRailLimits(bool on, int minMm, int maxMm) {
    nvs_handle_t nvs;
    if (nvs_open("ard-rail", NVS_READWRITE, &nvs) != ESP_OK) return;
    const bool ok = nvs_set_u8(nvs, "lim", on ? 1 : 0) == ESP_OK && nvs_set_i16(nvs, "lmin", (int16_t)minMm) == ESP_OK &&
                    nvs_set_i16(nvs, "lmax", (int16_t)maxMm) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    if (!ok) log_async("Rail limits not saved to NVS");
}
} // namespace

void motion_load_rail_settings() {
    nvs_handle_t nvs;
    if (nvs_open("ard-rail", NVS_READONLY, &nvs) != ESP_OK) return; // first boot: built-in defaults
    int8_t dir = 0;
    uint32_t bits = 0;
    const bool haveDir = nvs_get_i8(nvs, "dir", &dir) == ESP_OK && (dir == 1 || dir == -1);
    const bool haveSpm = nvs_get_u32(nvs, "spm", &bits) == ESP_OK;
    uint8_t limitOn = 1;
    int16_t limitMin = 0, limitMax = 0;
    const bool haveLimits = nvs_get_u8(nvs, "lim", &limitOn) == ESP_OK && nvs_get_i16(nvs, "lmin", &limitMin) == ESP_OK &&
                            nvs_get_i16(nvs, "lmax", &limitMax) == ESP_OK;
    nvs_close(nvs);
    float spm;
    memcpy(&spm, &bits, sizeof(spm));
    Lock lock(portMAX_DELAY);
    if (haveDir) railDir = dir;
    if (haveSpm && isfinite(spm) && spm > 0 && spm <= 100000) stepsPerMmZ = spm;
    if (haveLimits && limitMin >= 1 && limitMax <= 2000 && limitMax - limitMin >= 10) {
        railLimitOn = limitOn != 0;
        railMinMm = limitMin;
        railMaxMm = limitMax;
    }
    ESP_LOGI(TAG, "Rail: direction %+d, %.2f steps/mm, limits %s %d..%d mm", railDir, stepsPerMmZ,
             railLimitOn ? "on" : "OFF", railMinMm, railMaxMm);
}

bool motion_set_steps_per_mm(float factor) {
    if (!isfinite(factor) || factor <= 0 || factor > 100000) return false;
    int dir;
    bool changed;
    {
        Lock lock(portMAX_DELAY);
        changed = stepsPerMmZ != factor;
        stepsPerMmZ = factor;
        dir = railDir;
    }
    if (changed) saveRailSettings(dir, factor); // the PC resends it before every sorting run
    return true;
}

bool motion_set_rail_dir(int dir) {
    if (dir != 1 && dir != -1) return false;
    float spm;
    bool changed;
    {
        Lock lock(portMAX_DELAY);
        if (pidEnabled) return false; // flipping the sign under a running PID drives it away
        changed = railDir != dir;
        railDir = dir;
        spm = stepsPerMmZ;
    }
    if (changed) saveRailSettings(dir, spm);
    return true;
}

bool motion_set_rail_limits(bool on, int minMm, int maxMm) {
    if (on && (minMm < 1 || maxMm > 2000 || maxMm - minMm < 10)) return false;
    bool changed;
    {
        Lock lock(portMAX_DELAY);
        if (!on) { minMm = railMinMm; maxMm = railMaxMm; } // mzl0 keeps the values for mzl on again
        changed = railLimitOn != on || railMinMm != minMm || railMaxMm != maxMm;
        if (on && pidEnabled && (targetRailDist < minMm || targetRailDist > maxMm)) {
            pidEnabled = false; // the running PID target would leave the new range
            engine.stop(AXIS_Z);
            log_async("Rail PID OFF: target %d mm outside the new limits", targetRailDist);
        }
        railLimitOn = on;
        railMinMm = minMm;
        railMaxMm = maxMm;
        railLimitStopped = false;
    }
    if (changed) saveRailLimits(on, minMm, maxMm); // the PC resends it before every sorting run
    return true;
}

bool motion_status(MotionStatus *out, uint32_t timeoutMs) {
    Lock lock(pdMS_TO_TICKS(timeoutMs));
    if (!lock.held) return false;
    out->x = engine.position(AXIS_X);
    out->y = engine.position(AXIS_Y);
    out->z = engine.position(AXIS_Z);
    out->queued = uxQueueMessagesWaiting(queue);
    out->busy = executing || waitingForDelay || homing || engine.anyMoving() || out->queued > 0;
    out->program = executing || waitingForDelay || homing || out->queued > 0;
    out->stopped = stopActive;
    out->pid = pidEnabled;
    out->homing = homing;
    out->jogging = jogAny();
    out->stepsPerMmZ = stepsPerMmZ;
    out->pidP = pidP; out->pidI = pidI; out->pidD = pidD;
    out->targetRailDist = targetRailDist;
    out->railDir = railDir;
    out->railSettled = pidEnabled && railSettled;
    out->railLimitOn = railLimitOn;
    out->railMinMm = railMinMm;
    out->railMaxMm = railMaxMm;
    out->railLimitCount = railLimitCount;
    strlcpy(out->railLimitReason, railLimitReason, sizeof(out->railLimitReason));
    out->tiltDeg = tiltDeg(out->x, out->y);
    out->tiltLimitDeg = ARD_DISK_TILT_LIMIT_DEG;
    out->tiltLimitCount = tiltLimitCount;
    strlcpy(out->tiltLimitReason, tiltLimitReason, sizeof(out->tiltLimitReason));
    out->lastMoveMs = lastMoveMs;
    out->lastHomingMs = lastHomingMs;
    return true;
}

uint32_t motion_boot_id() { return bootId; }

bool motion_jog(float vx, float vy, float vz) {
    Lock lock(pdMS_TO_TICKS(20)); // a rejected jog disarms the remote control
    if (!lock.held) return false;
    const float v[3] = {vx, vy, vz};
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        jogInput[i] = isfinite(v[i]) ? fminf(fmaxf(v[i], -1.0f), 1.0f) : 0.0f;
        any = any || jogInput[i] != 0.0f;
    }
    jogStamp = nowMs();
    if (!any) return true; // decelerating stop
    // A queued program or homing owns the axes. The rail PID owns only Z.
    if (executing || homing || waitingForDelay || uxQueueMessagesWaiting(queue) > 0) {
        for (float &j : jogInput) j = 0;
        return false;
    }
    if (jogInput[AXIS_Z] != 0.0f && !railMotionAllowed(nowMs())) jogInput[AXIS_Z] = 0;
    if (jogInput[AXIS_Z] != 0.0f && pidEnabled) {
        // Moving the rail by hand is an explicit override of the ToF PID.
        pidEnabled = false;
        railSettled = false;
        log_async("Rail PID OFF: rail jog");
    }
    stopActive = false; // like the other move commands, a jog re-arms after E-STOP
    return true;
}
