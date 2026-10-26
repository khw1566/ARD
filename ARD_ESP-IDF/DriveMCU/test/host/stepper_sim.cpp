// Host simulation of StepperEngine: the planner runs every 1 ms and the step ISR
// 20 times per millisecond, as on the ESP32. Checks exact arrival, direction
// handling, speed limits, pulse timing and halts. Run with run_tests.cmd.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define private public
#include "../../main/stepper_engine.h"
#undef private
#include "../../main/stepper_engine.cpp"

GpioSim GPIO;

namespace {

constexpr gpio_num_t kStep = 18, kDir = 19, kStepY = 25, kDirY = 26;
int failures = 0;
long ticks = 0, lastStepTick = -100, minStepGap = 1 << 30;
long stepsUp = 0, stepsDown = 0, wrongDir = 0;
long yUp = 0, yDown = 0;

void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

void onWrite(uint32_t mask, bool set) {
    if (set && (mask & (1u << kStepY))) {
        if (GPIO.level & (1u << kDirY)) ++yUp; else ++yDown;
    }
    if (!(mask & (1u << kStep)) || !set) return;
    const bool dirHigh = GPIO.level & (1u << kDir);
    if (dirHigh) ++stepsUp; else ++stepsDown;
    minStepGap = std::min(minStepGap, ticks - lastStepTick);
    lastStepTick = ticks;
}

struct Sim {
    StepperEngine e;
    float peak = 0;
    Sim(float vmax, float accel) {
        const StepperEngine::Pins pins[3] = {{kStep, kDir, false}, {kStepY, kDirY, false}, {27, 14, false}};
        e.begin(pins);
        e.setMaxSpeed(0, vmax);
        e.setAcceleration(0, accel);
        stepsUp = stepsDown = 0;
        minStepGap = 1 << 30;
        lastStepTick = -100;
    }
    void ms(int count = 1) {
        for (int m = 0; m < count; ++m) {
            e.update(0.001f);
            peak = std::fmax(peak, std::fabs(e.speed(0)));
            for (uint32_t t = 0; t < StepperEngine::kTickHz / 1000; ++t, ++ticks)
                StepperEngine::onAlarm(nullptr, nullptr, &e);
        }
    }
    int runUntilIdle(int limitMs) {
        int elapsed = 0;
        do { ms(); ++elapsed; } while (e.moving(0) && elapsed < limitMs);
        return elapsed;
    }
};

} // namespace

int main() {
    GPIO.hook = onWrite;
    {
        Sim s(1000, 500);
        s.e.move(0, 1000);
        const int t = s.runUntilIdle(10000);
        check(s.e.position(0) == 1000, "move 1000 lands exactly on target");
        check(stepsUp == 1000 && stepsDown == 0, "1000 pulses, all with DIR high");
        check(s.peak <= 1000.5f, "speed never exceeds max speed");
        check(t > 2600 && t < 3400, "trapezoid duration ~3 s (v/a + d/v)");
        std::printf("     duration %d ms, peak %.1f steps/s\n", t, s.peak);
    }
    {
        Sim s(800, 400);
        s.e.move(0, -500);
        s.runUntilIdle(10000);
        check(s.e.position(0) == -500 && stepsDown == 500 && stepsUp == 0, "negative move uses DIR low");
    }
    {
        Sim s(1000, 500);
        s.e.move(0, 2000);
        s.ms(1500);                // cruising forward
        const int32_t at = s.e.position(0);
        s.e.moveTo(0, at - 300);   // target jumps behind the axis
        s.runUntilIdle(20000);
        check(s.e.position(0) == at - 300, "reversal brakes, returns and lands exactly");
        check(stepsUp - stepsDown == at - 300, "pulse count matches position after reversal");
    }
    {
        Sim s(1000, 500);
        s.e.move(0, 100000);
        s.ms(2500);                // at full speed
        const int32_t before = s.e.position(0);
        s.e.stop(0);
        s.runUntilIdle(10000);
        const int32_t brake = s.e.position(0) - before;
        check(brake >= 900 && brake <= 1100, "stop() decelerates over v^2/2a (~1000 steps)");
        std::printf("     braking distance %ld steps\n", (long)brake);
    }
    {
        Sim s(1000, 500);
        s.e.move(0, 100000);
        s.ms(1000);
        const int32_t before = s.e.position(0);
        s.e.haltAll();
        s.ms(50);
        check(s.e.position(0) == before && !s.e.moving(0), "haltAll stops pulses immediately");
    }
    {
        Sim s(10000, 20000);
        s.e.move(0, 20000);
        s.runUntilIdle(10000);
        check(s.e.position(0) == 20000, "10 kHz move lands exactly");
        check(minStepGap >= 2, "every step has a low tick between pulses");
        check(s.peak <= 10000.5f, "10000 steps/s cap respected");
    }
    {
        // Y-axis reversal hunt: random X+Y and Y-only moves with planner jitter (0.2..5 ms
        // between updates) and random decelerating stops, as the motion task sees them.
        // Every pulse of a move must carry that move's direction.
        Sim s(1000, 500);
        s.e.setMaxSpeed(1, 500);
        s.e.setAcceleration(1, 500);
        std::srand(12345);
        long reversed = 0, missed = 0;
        for (int n = 0; n < 400; ++n) {
            const int32_t start = s.e.position(1);
            int32_t rel = (std::rand() % 1600) - 800;
            if (!rel) rel = 1;
            s.e.move(0, -rel);
            s.e.move(1, rel);
            const bool stopEarly = std::rand() % 5 == 0;
            yUp = yDown = 0;
            for (int guard = 0; (s.e.moving(0) || s.e.moving(1)) && guard < 40000; ++guard) {
                const int jitterTicks = 4 + std::rand() % 97; // 0.2..5 ms of ISR ticks
                s.e.update(jitterTicks / (float)StepperEngine::kTickHz);
                for (int t = 0; t < jitterTicks; ++t, ++ticks) StepperEngine::onAlarm(nullptr, nullptr, &s.e);
                if (stopEarly && guard == 50) s.e.stop(1);
            }
            if ((rel > 0 && yDown) || (rel < 0 && yUp)) ++reversed;
            if (!stopEarly && s.e.position(1) != start + rel) ++missed;
        }
        check(reversed == 0, "Y never steps against the commanded direction (400 jittered moves)");
        check(missed == 0, "Y lands exactly on every jittered move");
    }
    {
        Sim s(1000, 500);
        s.e.setCurrentPosition(1, 1000);
        s.e.move(1, 2147483000);   // position + relative would wrap to a negative target
        check(s.e.distanceToGo(1) > 0, "huge positive move saturates instead of wrapping backwards");
        s.e.setCurrentPosition(1, -1000);
        s.e.move(1, -2147483000);
        check(s.e.distanceToGo(1) < 0, "huge negative move saturates instead of wrapping forwards");
    }
    {
        Sim s(1000, 500);
        s.e.setMaxSpeed(0, 50000);
        check(s.e.maxSpeed(0) == StepperEngine::kMaxStepRate, "max speed clamps to 10000 steps/s");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
