"""Python model of the DriveMCU Z safety limit guard (motion.cpp railLimitGuard). No compile.

Run: python -m unittest discover -s ARD_ESP-IDF/DriveMCU/test/model
The constants are read from motion.cpp so the model follows the firmware.
"""
from pathlib import Path
import re
import unittest

SRC = (Path(__file__).resolve().parents[2] / 'main' / 'motion.cpp').read_text(encoding='utf-8')


def const(name):
    return float(re.search(rf'constexpr float {name} = ([0-9.]+)f;', SRC).group(1))


LAG_S, EXTRA_MM, TOL_MM = const('kLimitTofLagS'), const('kLimitExtraMm'), const('kLimitToleranceMm')


def guard(v, togo, tof, rail_dir, spm, accel, lo, hi):
    """'run', 'decel' or 'halt' for one 1 ms tick, as railLimitGuard."""
    if v == 0 and togo == 0:
        return 'run'
    step_sign = 1 if v > 0 else -1 if v < 0 else (1 if togo > 0 else -1)
    tof_sign = rail_dir * step_sign
    margin = v * v / (2 * accel) / spm + LAG_S * abs(v) / spm + EXTRA_MM
    limit = hi + TOL_MM if tof_sign > 0 else lo - TOL_MM
    remaining = limit - tof if tof_sign > 0 else tof - limit
    if remaining > margin:
        return 'run'
    return 'halt' if remaining <= 0 else 'decel'


def simulate(start_tof, steps, rail_dir=-1, spm=23.683, vmax=500., accel=1000., lo=70, hi=120,
             tof_delay_s=0.15, sample_s=0.05, seconds=20.):
    """Trapezoidal Z move with a delayed, sampled ToF. Returns (final true ToF, stopped, max ToF seen)."""
    dt = .001
    z, v, target = 0., 0., steps
    history, sampled, next_sample = [], start_tof, 0.
    stopped, extreme = False, start_tof
    t = 0.
    while t < seconds:
        true_tof = start_tof + rail_dir * z / spm
        history.append((t, true_tof))
        if t >= next_sample:
            next_sample += sample_s
            old = [x for (s, x) in history if s <= t - tof_delay_s]
            sampled = old[-1] if old else start_tof
        # Like the firmware: one stop per event, then the braking target is held.
        action = 'run' if stopped else guard(v, target - z, sampled, rail_dir, spm, accel, lo, hi)
        if action == 'halt':
            target, v, stopped = z, 0., True
        elif action == 'decel':
            stopped = True
            brake = v * v / (2 * accel)
            target = z + (brake if v > 0 else -brake if v < 0 else 0)
        togo = target - z
        if abs(togo) < .5 and abs(v) < 1:
            v = 0.
            if not stopped and abs(togo) < .5:
                break
        else:
            want = vmax if togo > 0 else -vmax
            reach = (2 * accel * abs(togo)) ** .5
            want = max(-reach, min(reach, want))
            v += max(-accel * dt, min(accel * dt, want - v))
            z += v * dt
        extreme = max(extreme, true_tof) if rail_dir * steps > 0 else min(extreme, true_tof)
        t += dt
        if stopped and v == 0:
            break
    return start_tof + rail_dir * z / spm, stopped, extreme


class RailLimitModelTests(unittest.TestCase):
    def test_constants_found(self):
        self.assertEqual((LAG_S, EXTRA_MM, TOL_MM), (0.3, 1.0, 2.0))

    def test_full_speed_toward_max_stops_inside_tolerance(self):
        for rail_dir in (1, -1):
            # Steps whose ToF effect grows the distance by 100 mm (far past 120).
            final, stopped, extreme = simulate(95, rail_dir * 2400, rail_dir=rail_dir)
            self.assertTrue(stopped, rail_dir)
            self.assertLessEqual(extreme, 120 + TOL_MM, rail_dir)
            self.assertGreater(final, 110, 'stops near the limit, not far before it')

    def test_toward_min_stops(self):
        final, stopped, extreme = simulate(95, 2400, rail_dir=-1)  # mirrored rail: +steps shorten
        self.assertTrue(stopped)
        self.assertGreaterEqual(extreme, 70 - TOL_MM)

    def test_short_move_inside_range_is_not_stopped(self):
        final, stopped, _ = simulate(95, -int(10 * 23.683), rail_dir=-1)  # +10 mm, ends at 105
        self.assertFalse(stopped)
        self.assertAlmostEqual(final, 105, delta=.1)

    def test_pid_target_on_the_limit_is_reachable(self):
        final, stopped, _ = simulate(110, -int(10 * 23.683), rail_dir=-1, vmax=60)  # slow PID-like move to 120
        self.assertAlmostEqual(final, 120, delta=1)

    def test_recovery_away_from_a_limit_is_allowed(self):
        self.assertEqual(guard(-300, -500, 125, 1, 23.683, 1000, 70, 120), 'run')   # past max, moving down
        self.assertEqual(guard(300, 500, 125, -1, 23.683, 1000, 70, 120), 'run')    # mirrored: +steps shorten
        self.assertEqual(guard(300, 500, 125, 1, 23.683, 1000, 70, 120), 'halt')    # past max, moving further
        self.assertEqual(guard(0, 100, 121.5, 1, 23.683, 1000, 70, 120), 'decel')   # about to start toward it


if __name__ == '__main__':
    unittest.main()
