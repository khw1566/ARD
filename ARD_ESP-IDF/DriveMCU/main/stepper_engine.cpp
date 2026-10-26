#include "stepper_engine.h"
#include <math.h>
#include "esp_attr.h"
#include "esp_check.h"
#include "soc/gpio_struct.h"

static const char *TAG = "stepper";

// Targets are kept within +-2^30 so `target - position` can never overflow.
// A wrapped int32 target would land behind the axis and reverse it.
static constexpr int64_t kPositionLimit = 1 << 30;

static int32_t clampTarget(int64_t target) {
    return (int32_t)(target > kPositionLimit ? kPositionLimit : target < -kPositionLimit ? -kPositionLimit : target);
}

esp_err_t StepperEngine::begin(const Pins (&pins)[kAxes]) {
    for (int i = 0; i < kAxes; ++i) {
        // Direct GPIO.out_w1ts writes in the ISR only cover GPIO0..31.
        ESP_RETURN_ON_FALSE(pins[i].step < 32 && pins[i].dir < 32, ESP_ERR_INVALID_ARG, TAG, "step/dir pins must be < 32");
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << pins[i].step) | (1ULL << pins[i].dir);
        io.mode = GPIO_MODE_OUTPUT;
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
        gpio_set_level(pins[i].step, 0);
        gpio_set_level(pins[i].dir, 0);
        Shared &s = shared_[i];
        s.stepMask = 1u << pins[i].step;
        s.dirMask = 1u << pins[i].dir;
        s.invertDir = pins[i].invertDir;
        s.dir = 1;
        s.appliedDir = 0; // forces DIR to be written before the first step
    }

    gptimer_config_t config = {};
    config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    config.direction = GPTIMER_COUNT_UP;
    config.resolution_hz = 1000000;
    config.intr_priority = 3; // above Wi-Fi/lwIP so step timing stays even
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&config, &timer_), TAG, "timer");
    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = onAlarm;
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(timer_, &callbacks, this), TAG, "callbacks");
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = config.resolution_hz / kTickHz;
    alarm.reload_count = 0;
    alarm.flags.auto_reload_on_alarm = true;
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(timer_, &alarm), TAG, "alarm");
    ESP_RETURN_ON_ERROR(gptimer_enable(timer_), TAG, "enable");
    return gptimer_start(timer_);
}

// Runs every 50 us. Registers are written directly so the handler stays in IRAM.
bool IRAM_ATTR StepperEngine::onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *ctx) {
    auto *self = static_cast<StepperEngine *>(ctx);
    portENTER_CRITICAL_ISR(&self->lock_);
    for (int i = 0; i < kAxes; ++i) {
        Shared &s = self->shared_[i];
        if (s.pulseHigh) { // A4988 needs >= 1 us high; one tick gives 50 us.
            GPIO.out_w1tc = s.stepMask;
            s.pulseHigh = false;
            continue;
        }
        if (!s.rate || s.position == s.target) continue;
        if (s.dir != s.appliedDir) { // change DIR on its own tick (setup time > 200 ns)
            const bool high = (s.dir > 0) != s.invertDir;
            if (high) GPIO.out_w1ts = s.dirMask; else GPIO.out_w1tc = s.dirMask;
            s.appliedDir = s.dir;
            continue;
        }
        s.phase += s.rate;
        if (s.phase >= 65536u) {
            s.phase -= 65536u;
            GPIO.out_w1ts = s.stepMask;
            s.position = s.position + s.dir;
            s.pulseHigh = true;
        }
    }
    portEXIT_CRITICAL_ISR(&self->lock_);
    return false;
}

void StepperEngine::setMaxSpeed(int axis, float v) {
    plan_[axis].maxSpeed = fminf(fmaxf(fabsf(v), 1.0f), kMaxStepRate);
}

void StepperEngine::setAcceleration(int axis, float a) {
    plan_[axis].accel = fmaxf(fabsf(a), 1.0f);
}

int32_t StepperEngine::position(int axis) const {
    portENTER_CRITICAL(&lock_);
    int32_t p = shared_[axis].position;
    portEXIT_CRITICAL(&lock_);
    return p;
}

int32_t StepperEngine::target(int axis) const {
    portENTER_CRITICAL(&lock_);
    int32_t t = shared_[axis].target;
    portEXIT_CRITICAL(&lock_);
    return t;
}

bool StepperEngine::moving(int axis) const {
    return distanceToGo(axis) != 0 || plan_[axis].speed != 0.0f;
}

bool StepperEngine::anyMoving() const {
    for (int i = 0; i < kAxes; ++i) if (moving(i)) return true;
    return false;
}

void StepperEngine::moveTo(int axis, int32_t absolute) {
    portENTER_CRITICAL(&lock_);
    shared_[axis].target = clampTarget(absolute);
    portEXIT_CRITICAL(&lock_);
}

void StepperEngine::move(int axis, int32_t relative) {
    portENTER_CRITICAL(&lock_);
    shared_[axis].target = clampTarget((int64_t)shared_[axis].position + relative);
    portEXIT_CRITICAL(&lock_);
}

void StepperEngine::stop(int axis) {
    const Plan &p = plan_[axis];
    const float v = p.speed;
    const int32_t brake = (int32_t)ceilf(v * v / (2.0f * p.accel));
    portENTER_CRITICAL(&lock_);
    shared_[axis].target = clampTarget((int64_t)shared_[axis].position + (v > 0 ? brake : v < 0 ? -brake : 0));
    portEXIT_CRITICAL(&lock_);
}

void StepperEngine::setCurrentPosition(int axis, int32_t position) {
    portENTER_CRITICAL(&lock_);
    Shared &s = shared_[axis];
    s.position = position;
    s.target = position;
    s.rate = 0;
    s.phase = 0;
    portEXIT_CRITICAL(&lock_);
    plan_[axis].speed = 0;
}

void StepperEngine::haltAll() {
    portENTER_CRITICAL(&lock_);
    for (Shared &s : shared_) {
        s.rate = 0;
        s.target = s.position;
        s.phase = 0;
    }
    haltSeq_ = haltSeq_ + 1;
    portEXIT_CRITICAL(&lock_);
}

void StepperEngine::update(float dt) {
    portENTER_CRITICAL(&lock_);
    const uint32_t seq = haltSeq_;
    portEXIT_CRITICAL(&lock_);
    if (seq != seenHaltSeq_) { // a halt happened outside the planner
        seenHaltSeq_ = seq;
        for (Plan &p : plan_) p.speed = 0;
    }
    for (int i = 0; i < kAxes; ++i) updateAxis(i, dt);
}

void StepperEngine::updateAxis(int axis, float dt) {
    Plan &p = plan_[axis];
    portENTER_CRITICAL(&lock_);
    const int32_t dist = shared_[axis].target - shared_[axis].position;
    const uint32_t seq = haltSeq_;
    portEXIT_CRITICAL(&lock_);

    float v = p.speed;
    const float dv = p.accel * dt;
    if (dist == 0) {
        v = 0; // the ISR has already stopped on the target step
    } else {
        const float sign = dist > 0 ? 1.0f : -1.0f;
        if (v != 0 && (v > 0) != (dist > 0)) {
            // Still moving away from a target that jumped behind us: brake first.
            v = fabsf(v) <= dv ? 0 : v - copysignf(dv, v);
        } else {
            const float reachable = sqrtf(2.0f * p.accel * fabsf((float)dist));
            const float limit = fminf(p.maxSpeed, reachable);
            float magnitude = fabsf(v);
            magnitude = magnitude < limit ? fminf(limit, magnitude + dv) : fmaxf(limit, magnitude - dv);
            v = sign * magnitude;
        }
    }
    const uint32_t rate = (uint32_t)(fabsf(v) * (65536.0f / kTickHz));
    portENTER_CRITICAL(&lock_);
    if (seq == haltSeq_) { // discard if a halt landed while we computed
        Shared &s = shared_[axis];
        s.rate = rate;
        if (v > 0) s.dir = 1; else if (v < 0) s.dir = -1;
        p.speed = v;
    } else {
        p.speed = 0;
    }
    portEXIT_CRITICAL(&lock_);
}
