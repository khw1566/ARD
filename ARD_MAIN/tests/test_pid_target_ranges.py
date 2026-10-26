"""PID target ranges on every screen allow the sequence origin (120 mm) and the bins (e.g. 70 mm).

Source checks only: no firmware build.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PidTargetRangeTests(unittest.TestCase):
    def test_hmi_firmware_and_simulator_ranges_match_and_allow_origin(self):
        c = (ROOT / 'ARD_ESP-IDF/HMI_MCU/main/hmi_ui.c').read_text(encoding='utf-8')
        js = (ROOT / 'ARD_MAIN/frontend/static/js/hmi-simulator.js').read_text(encoding='utf-8')
        c_min = int(re.search(r'#define PID_MIN (\d+)', c).group(1))
        c_max = int(re.search(r'#define PID_MAX (\d+)', c).group(1))
        js_min, js_max = map(int, re.search(r'PID_MIN = (\d+), PID_MAX = (\d+)', js).groups())
        self.assertEqual((c_min, c_max), (js_min, js_max))
        self.assertEqual((c_min, c_max), (70, 120))  # Z safety limits (ToF mm)
        model = (ROOT / 'ARD_ESP-IDF/HMI_MCU/main/hmi_model.c').read_text(encoding='utf-8')
        self.assertIn('m->pid_min = 70;', model)
        self.assertIn('m->pid_max = 120;', model)
        self.assertIn('"rail_min"', (ROOT / 'ARD_ESP-IDF/HMI_MCU/main/hmi_net.c').read_text(encoding='utf-8'))

    def test_pc_slider_and_jc1060_clamps(self):
        html = (ROOT / 'ARD_MAIN/frontend/templates/components/sidebar.html').read_text(encoding='utf-8')
        lo, hi = map(int, re.search(r'id="pid-target-slider" min="(\d+)" max="(\d+)"', html).groups())
        self.assertEqual((lo, hi), (70, 120))
        link = (ROOT / 'ARD_ESP-IDF/JC1060P470C/main/pc_link.cpp').read_text(encoding='utf-8')
        self.assertIn('"rail_min_mm", 70', link)
        self.assertIn('"rail_max_mm", 120', link)
        self.assertIn('"home_mm"', link)

    def test_drivemcu_firmware_limits_and_validation(self):
        motion = (ROOT / 'ARD_ESP-IDF/DriveMCU/main/motion.cpp').read_text(encoding='utf-8')
        self.assertIn('int railMinMm = 70, railMaxMm = 120;', motion)
        valid = (ROOT / 'ARD_ESP-IDF/DriveMCU/main/CommandValidation.h').read_text(encoding='utf-8')
        self.assertIn('"mzl"', valid)


if __name__ == '__main__':
    unittest.main()
