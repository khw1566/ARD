"""DriveMCU live ToF/rail state polling; no board or network access."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core.drive_live import DriveLive


def status(**values):
    return json.dumps({'protocol': 1, 'boot_id': 1, 'busy': False, 'pid': False, 'tof_mm': 160.0,
                       'tof_ok': True, 'tof_status': 'OK', 'rail_target': 145, 'rail_settled': False, **values})


class DriveLiveTests(unittest.TestCase):
    def setUp(self):
        self.now = 100.0
        self.reads = []
        self.answer = status()
        self.published = []
        self.enabled = True

        def read(ip):
            self.reads.append(ip)
            if isinstance(self.answer, Exception):
                raise self.answer
            return self.answer
        self.live = DriveLive(read, lambda: '10.0.0.22', lambda: self.enabled,
                              lambda snap, changed: self.published.append((snap, changed)),
                              period=0.3, clock=lambda: self.now)

    def test_poll_publishes_board_state_and_changes_only(self):
        self.assertTrue(self.live.poll_once())
        snap, changed = self.published[-1]
        self.assertEqual(snap['tof_mm'], 160.0)
        self.assertTrue(snap['live'])
        self.assertIn('pid', changed)
        self.now += 0.3
        self.live.poll_once()
        self.assertEqual(len(self.published), 1)  # nothing changed
        self.answer = status(tof_mm=161.0, pid=True, rail_target=150)
        self.now += 0.3
        self.live.poll_once()
        self.assertEqual(self.published[-1][1], {'tof_mm', 'pid', 'rail_target'})

    def test_fresh_sample_from_jog_or_rail_tool_skips_poll(self):
        self.live.record_text(status())
        self.now += 0.1
        self.assertFalse(self.live.poll_once())
        self.assertEqual(self.reads, [])
        self.now += 0.3
        self.assertTrue(self.live.poll_once())

    def test_disabled_offline_and_failure(self):
        self.enabled = False
        self.assertFalse(self.live.poll_once())
        self.enabled = True
        self.answer = OSError('timeout')
        self.assertFalse(self.live.poll_once())
        self.assertEqual(self.live.failures, 1)
        self.assertEqual(self.live.snapshot()['live'], False)

    def test_older_firmware_is_not_polled_again(self):
        self.answer = 'ERR:REJECTED'
        self.assertFalse(self.live.poll_once())
        self.now += 1
        self.assertFalse(self.live.poll_once())
        self.assertEqual(len(self.reads), 1)
        self.assertEqual(self.published, [])

    def test_sample_goes_stale(self):
        self.live.poll_once()
        self.now += 2.5
        self.assertFalse(self.live.snapshot()['live'])
        self.assertEqual(self.live.snapshot()['age_ms'], 2500)

    def test_board_pid_gains_are_published_and_updated(self):
        self.answer = status(pid_p=.12, pid_i=0, pid_d=0)
        self.live.poll_once()
        self.assertEqual(self.live.snapshot()['pid_p'], .12)
        self.now += 1
        self.answer = status(pid_p=.15, pid_i=0, pid_d=0)
        self.live.poll_once()
        self.assertEqual(self.published[-1][0]['pid_p'], .15)
        self.assertIn('pid_p', self.published[-1][1])

    def test_board_positions_are_published(self):
        self.answer = status(x=120, y=-80, z=300)
        self.live.poll_once()
        snap, changed = self.published[-1]
        self.assertEqual((snap['x'], snap['y'], snap['z']), (120, -80, 300))
        self.assertTrue({'x', 'y', 'z'} <= changed)
        self.assertTrue(self.live.board_live())
        self.now += 2.5
        self.assertFalse(self.live.board_live())  # stale: PC estimates take over again

    def test_jog_samples_are_published_and_final_read_after_release(self):
        jogging = {'active': True}
        self.live.enabled = lambda: not jogging['active']
        # The jog route reads status itself; those samples are pushed too.
        self.live.record_text(status(x=10, y=10, z=0, jogging=True))
        self.assertEqual(self.published[-1][0]['x'], 10)
        self.now += 1
        self.assertFalse(self.live.poll_once())  # no extra poll while jogging
        jogging['active'] = False
        self.answer = status(x=55, y=55, z=0, jogging=False)
        self.assertTrue(self.live.poll_once())  # first poll after release reads the final position
        snap, changed = self.published[-1]
        self.assertEqual((snap['x'], snap['jogging']), (55, False))
        self.assertIn('x', changed)


class BoardPositionAppTests(unittest.TestCase):
    """publish_drive_live / update_motor_state from app.py, executed without Flask or a board."""
    def setUp(self):
        import ast
        from copy import deepcopy
        app = Path(__file__).resolve().parents[1] / 'backend' / 'app.py'
        tree = ast.parse(app.read_text(encoding='utf-8-sig'))
        nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef)
                 and n.name in ('apply_board_position', 'publish_drive_live', 'update_motor_state')]
        self.events = []
        self.env = {'motor_state': dict(mx_steps=0, my_steps=0, mz_steps=0, mx_deg=0., my_deg=0., mz_deg=0.,
                                        roll_deg=0., yaw_deg=0., tof_dist=0),
                    'device_stats': {'DriveMCU': {}}, 'drive_settings': {},
                    'CONFIG': {'gear_ratio_x': 4., 'gear_ratio_y': 4., 'gear_ratio_z': 1.},
                    'broadcast_event': self.events.append, 'deepcopy': deepcopy}
        exec(compile(ast.Module(body=nodes, type_ignores=[]), str(app), 'exec'), self.env)
        self.board = DriveLive(lambda ip: status(x=200, y=-200, z=435), lambda: 'ip', lambda: True,
                               self.env['publish_drive_live'], clock=lambda: 0.)
        self.env['drive_live'] = self.board

    def test_board_steps_replace_command_estimate(self):
        self.board.poll_once()
        s = self.env['motor_state']
        self.assertEqual((s['mx_steps'], s['my_steps'], s['mz_steps']), (200, -200, 435))
        self.assertEqual((s['roll_deg'], s['yaw_deg']), (90., 0.))
        update = [e for e in self.events if e['type'] == 'drive_update']
        self.assertEqual(update[-1]['source'], 'board')
        self.assertEqual(update[-1]['state']['mz_steps'], 435)
        self.assertEqual(self.events[-1]['type'], 'drive_live')
        # A command acknowledged by the board is not added again on top of the real position.
        self.env['update_motor_state']('b100,100')
        self.assertEqual(s['mx_steps'], 200)
        self.env['update_motor_state']('mzt150')  # settings still follow the command
        self.assertEqual(self.env['drive_settings']['pid_target_tof'], 150)

    def test_without_board_status_commands_update_the_estimate(self):
        self.env['update_motor_state']('b100,100')
        self.assertEqual(self.env['motor_state']['mx_steps'], 100)


if __name__ == '__main__':
    unittest.main()
