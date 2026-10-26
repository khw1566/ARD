"""Dedicated JC1060 rail endpoint against an offline fake board. No builds/network."""
import unittest
from flask import Flask, request
from test_rail_control import RailBoard, tools_for
from api.rail_routes import register_rail_routes


class TerminalRailTests(unittest.TestCase):
    def setUp(self):
        self.board = RailBoard()
        self.tools = tools_for(self.board)
        self.tools.update({'taught': {'center': True, 'plus': True, 'minus': True}})
        self.sorting = False
        self.selected = True
        self.app = Flask(__name__)
        register_rail_routes(self.app, self.tools, lambda: 'board', lambda: self.sorting,
                             allowed_terminal=lambda: self.selected and request.remote_addr == '10.0.0.9')
        self.client = self.app.test_client()
        self.client.environ_base['REMOTE_ADDR'] = '10.0.0.9'

    def post(self, **body):
        return self.client.post('/api/jc1060/rail', json=body)

    def test_mode_selection_stops_pid_without_starting_motion(self):
        self.board.state['pid'] = True
        response = self.post(op='mode', mode='open_loop')
        self.assertEqual(response.status_code, 200)
        self.assertEqual(self.tools.snapshot()['mode'], 'open_loop')
        self.assertEqual(self.board.commands, ['mzm0'])
        self.assertFalse(self.board.state['pid'])
        self.assertEqual(self.post(op='mode', mode='pid').status_code, 200)
        self.assertEqual(self.board.commands, ['mzm0', 'mzm0'])

    def test_pid_target_uses_saved_gains_and_explicit_start(self):
        response = self.post(op='target', mm=95)
        self.assertEqual(response.status_code, 200, response.json)
        self.assertEqual(self.board.commands[-2:], ['mzt95', 'mzm1'])
        self.assertIn('mzp0.12', self.board.commands)
        self.assertTrue(self.board.state['pid'])

    def test_open_loop_step_works_without_tof_and_pid_target_is_rejected(self):
        self.post(op='mode', mode='open_loop')
        self.board.state['tof_ok'] = False
        self.assertEqual(self.post(op='step', steps=-50).status_code, 200)
        self.assertEqual(self.board.commands[-1], 'mzr-50')
        self.assertEqual(self.post(op='target', mm=95).status_code, 409)

    def test_tof_failure_or_jogging_blocks_pid_start(self):
        for change in ({'tof_ok': False}, {'jogging': True}, {'stopped': True}):
            self.board.state.update(tof_ok=True, jogging=False, stopped=False)
            self.board.state.update(change)
            with self.subTest(change=change):
                self.assertEqual(self.post(op='target', mm=95).status_code, 409)
        self.assertEqual(self.board.commands, [])

    def test_sorting_and_other_peers_cannot_operate(self):
        self.sorting = True
        self.assertEqual(self.post(op='mode', mode='pid').status_code, 409)
        self.sorting = False
        self.client.environ_base['REMOTE_ADDR'] = '10.0.0.10'
        self.assertEqual(self.post(op='mode', mode='pid').status_code, 403)
        self.client.environ_base['REMOTE_ADDR'] = '10.0.0.9'
        self.selected = False
        self.assertEqual(self.post(op='mode', mode='pid').status_code, 403)
        self.assertEqual(self.board.commands, [])

    def test_invalid_target_or_step_does_not_send_commands(self):
        self.assertEqual(self.post(op='target', mm=500).status_code, 400)
        self.assertEqual(self.board.commands, [])
        self.post(op='mode', mode='open_loop')
        before = list(self.board.commands)
        for steps in (True, 0, 2001, 1.5):
            self.assertEqual(self.post(op='step', steps=steps).status_code, 400)
        self.assertEqual(self.board.commands, before)

    def test_rejected_command_does_not_persist_mode(self):
        self.tools.transport = lambda ip, cmd: 'ERR:REJECTED' if cmd == 'mzm0' else self.board(ip, cmd)
        self.assertEqual(self.post(op='mode', mode='open_loop').status_code, 409)
        self.assertEqual(self.tools.snapshot()['mode'], 'pid')

    def test_pid_mode_rejects_step_motion(self):
        self.assertEqual(self.post(op='step', steps=50).status_code, 409)
        self.assertEqual(self.board.commands, [])


if __name__ == '__main__':
    unittest.main()
