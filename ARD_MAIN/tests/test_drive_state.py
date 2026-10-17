"""Exercise the real state updater without importing Flask or contacting MCUs."""
import ast
from copy import deepcopy
from pathlib import Path
import unittest

APP = Path(__file__).resolve().parents[1] / 'backend' / 'app.py'

class DriveStateTests(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(APP.read_text(encoding='utf-8-sig'))
        fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'update_motor_state')
        self.events = []
        self.env = {'motor_state': dict(mx_steps=0, my_steps=0, mz_steps=0, mx_deg=0., my_deg=0., mz_deg=0., roll_deg=0., yaw_deg=0.),
                    'CONFIG': {'gear_ratio_x': 4., 'gear_ratio_y': 4., 'gear_ratio_z': 1.},
                    'drive_settings': {}, 'broadcast_event': self.events.append, 'deepcopy': deepcopy}
        exec(compile(ast.Module(body=[fn], type_ignores=[]), str(APP), 'exec'), self.env)

    def command(self, cmd):
        self.env['update_motor_state'](cmd)
        return self.env['motor_state']

    def test_same_sign_selects_yaw(self):
        s = self.command('b200,200')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (90., 0.))

    def test_opposite_sign_selects_roll(self):
        s = self.command('b200,-200')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (0., 90.))

    def test_direct_firmware_commands(self):
        s = self.command('dr200')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (90., 0.))
        self.command('dl200')
        s = self.command('df100')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (0., 45.))
        s = self.command('db100')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (0., 0.))

    def test_sequence_and_return(self):
        self.command('b200,200')
        s = self.command('b100,-100')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (90., 45.))
        self.command('b-100,100')
        s = self.command('b-200,-200')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (0., 0.))

    def test_single_axis_and_unequal_ratios(self):
        self.env['CONFIG']['gear_ratio_y'] = 2.
        self.command('mxr200')
        s = self.command('myr100')
        self.assertEqual((s['yaw_deg'], s['roll_deg']), (90., 0.))
        self.assertEqual(len(self.events), 2)

if __name__ == '__main__':
    unittest.main()
