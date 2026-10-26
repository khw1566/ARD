"""Disc tilt safety limit (±60°, user spec 2026-10-27): PC pre-check of manual and sequence moves.
Disc rotation (X + Y) is exempt. The DriveMCU enforces the same rule on every move and jog."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core.rail_control import TILT_LIMIT_DEG, disc_command_steps, disc_tilt_deg, disc_tilt_error, tilt_steps

RATIOS = (4.0, 4.0)  # 4:1 reducers, 1.8 deg/step: 0.45 deg per motor step


class DiscTiltLimitTest(unittest.TestCase):
    def test_command_steps(self):
        self.assertEqual(disc_command_steps('df100'), (100, -100))
        self.assertEqual(disc_command_steps('DB'), (-200, 200))
        self.assertEqual(disc_command_steps('dr50'), (50, 50))
        self.assertEqual(disc_command_steps('b10,-20'), (10, -20))
        self.assertEqual(disc_command_steps('mxr-30'), (-30, 0))
        self.assertEqual(disc_command_steps('myr30'), (0, 30))
        for other in ('bv100', 'mzr100', 'home', 'x', 'j100,0,0', 'bxx,1'):
            self.assertIsNone(disc_command_steps(other), other)

    def test_sequence_tilt_45_is_inside(self):
        rx, ry = tilt_steps(45, RATIOS)
        self.assertEqual((rx, ry), (100, -100))
        self.assertAlmostEqual(disc_tilt_deg(rx, ry, RATIOS), 45)
        self.assertIsNone(disc_tilt_error(f'b{rx},{ry}', 0, 0, RATIOS))
        with self.assertRaises(RuntimeError):
            tilt_steps(61, RATIOS)

    def test_limit_on_tilt_only(self):
        self.assertIsNone(disc_tilt_error('df133', 0, 0, RATIOS))       # 59.85 deg
        self.assertIsNotNone(disc_tilt_error('df134', 0, 0, RATIOS))    # 60.3 deg
        self.assertIsNotNone(disc_tilt_error('db134', 0, 0, RATIOS))
        self.assertIsNotNone(disc_tilt_error('mxr267', 0, 0, RATIOS))   # one motor tilts too
        self.assertIsNone(disc_tilt_error('dr100000', 0, 0, RATIOS))    # rotation is free
        self.assertIsNone(disc_tilt_error('dr500', 100, -100, RATIOS))  # rotating while tilted 45
        self.assertIsNotNone(disc_tilt_error('df50', 100, -100, RATIOS))  # 45 + 22.5

    def test_back_toward_level_from_outside(self):
        # A stale position outside the limit must still be able to come back.
        self.assertIsNone(disc_tilt_error('db10', 150, -150, RATIOS))     # 67.5 -> 63
        self.assertIsNotNone(disc_tilt_error('db400', 150, -150, RATIOS))  # crosses to -112.5
        self.assertEqual(TILT_LIMIT_DEG, 60)


if __name__ == '__main__':
    unittest.main()
