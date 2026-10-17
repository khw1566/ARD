#include "motion.h"
#include <math.h>
#include <atomic>
#include "board.h"
#include "stepper_engine.h"
#include "sensors.h"
#include "telemetry.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
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

// Y/Z may share X's EN line (Kconfig -1): the shared line stays low while any axis on it is enabled.
constexpr gpio_num_t kEnablePins[3] = {PIN_EN_X, PIN_EN_Y >= 0 ? PIN_EN_Y : PIN_EN_X, PIN_EN_Z >= 0 ? PIN_EN_Z : PIN_EN_X};
constexpr uint32_t kJogTimeoutMs = 300;   // jog input must be refreshed faster than this
constexpr int32_t kJogLead = 20000;       // steps of target kept ahead of a jogging axis
constexpr TickType_t kLockTimeout = pdMS_TO_TICKS(100);
constexpr uint32_t kPidPeriodMs = 50;
constexpr uint32_t kTofMaxAgeMs = 200;

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

int targetRailDist = 140;
float stepsPerMmZ = 43.48f;
float pidP = 0.3f, pidI = 0.01f, pidD = 5.0f;
float integralRail = 0, lastRailError = 0;
uint32_t lastPid = 0;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

struct Lock {
    bool held;
    explicit Lock(TickType_t timeout = kLockTimeout) : held(xSemaphoreTake(mutex, timeout) == pdTRUE) {}
    ~Lock() { if (held) xSemaphoreGive(mutex); }
};

std::atomic<uint32_t> enabledMask{0};

void setEnable(int axis, bool enabled) {
    const uint32_t mask = enabled ? enabledMask.fetch_or(1u << axis) | (1u << axis)
                                  : enabledMask.fetch_and(~(1u << axis)) & ~(1u << axis);
    bool anyOnPin = false;
    for (int i = 0; i < 3; ++i)
        if (kEnablePins[i] == kEnablePins[axis] && (mask & (1u << i))) anyOnPin = true;
    gpio_set_level(kEnablePins[axis], anyOnPin ? 0 : 1);
}

// Gamepad / touch jog: per-motor velocity in -1..1 of the jog speed limit.
float jogInput[3] = {0, 0, 0};
uint32_t jogStamp = 0;
bool jogging[3] = {false, false, false};
float jogLimit[3] = {1000, 1000, 1000};   // steps/s at full stick
float configuredSpeed[3] = {1000, 1000, 1000};

bool jogAny() { return jogging[0] || jogging[1] || jogging[2]; }

void startAction(const MotorAction &a, uint32_t now) {
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
    if (!pidEnabled || stopActive) { lastRailError = 0; integralRail = 0; return; }
    if (now - lastPid < kPidPeriodMs) return;
    const float dt = (now - lastPid) / 1000.0f;
    lastPid = now;
    float distance;
    uint32_t age;
    if (!sensors_tof(&distance, &age) || age > kTofMaxAgeMs) {
        if (engine.distanceToGo(AXIS_Z) != 0) engine.stop(AXIS_Z); // no feedback: hold the rail
        return;
    }
    if (distance <= 0 || distance >= 2000) return;
    const float error = (float)targetRailDist - distance;
    if (fabsf(error) <= 2.0f) { // dead band
        if (engine.distanceToGo(AXIS_Z) != 0) engine.stop(AXIS_Z);
        integralRail = 0;
        lastRailError = error;
        return;
    }
    float p = pidP;
    if (fabsf(error) < 3.0f) p *= 0.5f; // soft landing
    const float pTerm = error * stepsPerMmZ * p;
    if (fabsf(error) < 10.0f) integralRail = fminf(fmaxf(integralRail + error * dt, -50.0f), 50.0f);
    else integralRail = 0;
    const float iTerm = integralRail * stepsPerMmZ * pidI;
    const float dTerm = ((error - lastRailError) / dt) * stepsPerMmZ * pidD * 0.01f;
    lastRailError = error;
    const int32_t delta = (int32_t)(pTerm + iTerm + dTerm);
    if (delta) {
        setEnable(AXIS_Z, true);
        engine.moveTo(AXIS_Z, engine.position(AXIS_Z) + delta);
    }
}

void jogTick(uint32_t now) {
    const bool fresh = now - jogStamp <= kJogTimeoutMs;
    for (int i = 0; i < 3; ++i) {
        const float v = fresh && !stopActive ? jogInput[i] : 0.0f;
        if (v != 0.0f) {
            if (!jogging[i]) { jogging[i] = true; executing = false; }
            setEnable(i, true);
            engine.setMaxSpeed(i, fabsf(v) * jogLimit[i]);
            engine.moveTo(i, engine.position(i) + (v > 0 ? kJogLead : -kJogLead));
        } else if (jogging[i]) {
            jogging[i] = false;
            engine.stop(i); // decelerate with the axis acceleration
            engine.setMaxSpeed(i, configuredSpeed[i]);
        }
    }
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
    railPid(now);
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
    const int speed[3] = {1000, 1000, 1000}, accel[3] = {500, 500, 1000};
    for (int i = 0; i < 3; ++i) {
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
    MotorAction a = {{x, y, z}, generation, axis};
    if (xQueueSend(queue, &a, 0) != pdPASS) {
        log_async("Command Queue FULL! Action rejected.");
        return false;
    }
    return true;
}

void motion_emergency_stop() {
    // Stop pulses and drivers first so STOP never waits behind the planner lock.
    engine.haltAll();
    for (int i = 0; i < 3; ++i) setEnable(i, false);
    stopActive = true;
    pidEnabled = false;
    Lock lock(portMAX_DELAY);
    ++generation; // invalidates an action already taken from the queue
    xQueueReset(queue);
    waitingForDelay = false;
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
    if (v > StepperEngine::kMaxStepRate)
        log_async("Speed %d limited to %d steps/s", v, (int)StepperEngine::kMaxStepRate);
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
    ++generation;
    xQueueReset(queue);
    waitingForDelay = false;
    stopActive = false;
    pidEnabled = false;
    setEnable(AXIS_X, true);
    setEnable(AXIS_Y, true);
    engine.moveTo(AXIS_X, 0);
    engine.moveTo(AXIS_Y, 0);
    homing = true;
    homingStart = nowMs();
    return true;
}

void motion_clear_stop() { stopActive = false; }

void motion_set_inter_command_delay(uint32_t ms) {
    Lock lock(portMAX_DELAY);
    interCommandDelay = ms;
}

void motion_set_pid_p(float p) { Lock lock(portMAX_DELAY); pidP = p; }
void motion_set_pid_i(float i) { Lock lock(portMAX_DELAY); pidI = i; }
void motion_set_pid_d(float d) { Lock lock(portMAX_DELAY); pidD = d; }
void motion_set_rail_target(int mm) { Lock lock(portMAX_DELAY); targetRailDist = mm; }

bool motion_set_pid_enabled(bool enabled) {
    if (enabled && !sensors_tof_ready()) return false;
    Lock lock(portMAX_DELAY);
    if (enabled) { integralRail = 0; lastPid = nowMs(); }
    pidEnabled = enabled;
    return true;
}

bool motion_set_steps_per_mm(float factor) {
    if (!isfinite(factor) || factor <= 0 || factor > 100000) return false;
    Lock lock(portMAX_DELAY);
    stepsPerMmZ = factor;
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
    out->stopped = stopActive;
    out->pid = pidEnabled;
    out->homing = homing;
    out->jogging = jogAny();
    out->enabledMask = enabledMask;
    out->stepsPerMmZ = stepsPerMmZ;
    out->targetRailDist = targetRailDist;
    out->lastMoveMs = lastMoveMs;
    out->lastHomingMs = lastHomingMs;
    return true;
}

uint32_t motion_boot_id() { return bootId; }

bool motion_jog(float vx, float vy, float vz) {
    Lock lock(pdMS_TO_TICKS(5));
    if (!lock.held) return false;
    const float v[3] = {vx, vy, vz};
    for (int i = 0; i < 3; ++i) jogInput[i] = fminf(fmaxf(v[i], -1.0f), 1.0f);
    jogStamp = nowMs();
    // A queued program or homing owns the axes; jog waits until they finish.
    if (executing || homing || waitingForDelay || uxQueueMessagesWaiting(queue) > 0) {
        for (float &j : jogInput) j = 0;
        return false;
    }
    return !stopActive;
}

void motion_set_jog_limit(Axis axis, float stepsPerSecond) {
    Lock lock(portMAX_DELAY);
    jogLimit[axis] = fminf(fmaxf(stepsPerSecond, 1.0f), StepperEngine::kMaxStepRate);
}

float motion_jog_limit(Axis axis) { return jogLimit[axis]; }
bool motion_stopped() { return stopActive; }
