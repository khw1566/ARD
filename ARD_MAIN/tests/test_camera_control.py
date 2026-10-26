"""Offline capture transport, frame correlation and retry/reset regression tests."""
import sys
import threading
import unittest
from types import SimpleNamespace
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core.camera_control import CameraControl
from core.sorting_pipeline import FrameInbox, SortingPipeline
from core.rail_control import validate
from test_rail_control import RailBoard, FastEvent


class CameraCommandTests(unittest.TestCase):
    def test_ambiguous_first_ack_failure_keeps_tagged_fallback(self):
        polls = []
        def offline(*args): raise OSError('ACK lost')
        camera = CameraControl(lambda: 'cam', polls.append, offline)
        result = camera.capture()
        self.assertEqual(polls, ['capture ' + result['capture_id']])
    def test_lost_ack_retries_same_id(self):
        commands, polls = [], []
        def transport(ip, command):
            commands.append(command)
            if len(commands) == 1:
                raise OSError('ACK lost')
            return 'ACK:CAPTURE_START,' + command.split()[1]
        result = CameraControl(lambda: 'cam', polls.append, transport).capture()
        self.assertEqual(commands[0], commands[1])
        self.assertEqual(result['capture_id'], commands[0].split()[1])
        self.assertEqual(polls, [])

    def test_busy_does_not_enqueue_poll_duplicate(self):
        polls = []
        camera = CameraControl(lambda: 'cam', polls.append, lambda *a: 'ERR:BUSY')
        with self.assertRaises(RuntimeError):
            camera.capture()
        self.assertEqual(polls, [])

    def test_old_firmware_uses_explicit_legacy_fallback(self):
        commands = []
        def transport(ip, command):
            commands.append(command)
            return 'ERR:UNKNOWN' if command.startswith('capture ') else 'ACK:CAPTURE_START'
        result = CameraControl(lambda: 'cam', lambda *a: self.fail(), transport).capture()
        self.assertEqual(result['channel'], 'legacy_tcp')
        self.assertNotIn('capture_id', result)
        self.assertEqual(commands[1], 'c')

    def test_known_new_firmware_poll_fallback_keeps_id(self):
        polls = []
        def offline(*a): raise OSError('offline')
        camera = CameraControl(lambda: 'cam', polls.append, offline)
        camera.supports_ids = True
        result = camera.capture()
        self.assertEqual(polls, ['capture ' + result['capture_id']])

    def test_reset_waits_for_driver_completion(self):
        replies = iter(['ACK:RESET_PENDING', 'ACK:PONG,busy=1,reset=1,ready=0', 'ACK:PONG,busy=0,reset=0,ready=1'])
        commands = []
        def transport(ip, command):
            commands.append(command)
            return next(replies)
        camera = CameraControl(lambda: 'cam', lambda *a: None, transport, pause=lambda t: None)
        self.assertTrue(camera.reset())
        self.assertEqual(commands, ['reset', 'ping', 'ping'])

    def test_unsupported_reset_is_not_reported_as_success(self):
        camera = CameraControl(lambda: 'cam', lambda *a: None, lambda *a: 'ERR:UNKNOWN')
        with self.assertRaises(RuntimeError): camera.reset()


class FrameCorrelationTests(unittest.TestCase):
    def test_matching_capture_survives_later_preview(self):
        inbox = FrameInbox()
        def trigger():
            inbox.publish(b'correct', '42')
            inbox.publish(b'preview')
            return {'capture_id': '42'}
        self.assertEqual(inbox.capture(trigger, threading.Event()), b'correct')

    def test_frame_can_publish_before_trigger_ack_without_deadlock(self):
        inbox = FrameInbox()
        def trigger():
            worker = threading.Thread(target=lambda: inbox.publish(b'new', '42'))
            worker.start(); worker.join(.5)
            self.assertFalse(worker.is_alive(), 'trigger must not hold the inbox lock')
            return {'capture_id': '42'}
        self.assertEqual(inbox.capture(trigger, threading.Event(), timeout=.6), b'new')

    def test_late_previous_capture_does_not_satisfy_new_capture(self):
        inbox = FrameInbox()
        def trigger():
            inbox.publish(b'previous', '41')
            return {'capture_id': '42'}
        with self.assertRaises(RuntimeError):
            inbox.capture(trigger, threading.Event(), timeout=.01)

    def test_untagged_preview_does_not_satisfy_tagged_capture(self):
        inbox = FrameInbox()
        def trigger():
            inbox.publish(b'preview')
            return {'capture_id': '42'}
        with self.assertRaises(RuntimeError):
            inbox.capture(trigger, threading.Event(), timeout=.01)


class SortingRecoveryTests(unittest.TestCase):
    def make_pipeline(self):
        self.board = RailBoard()
        self.events, self.resets = [], []
        p = SortingPipeline(FrameInbox(), lambda: None, lambda frame: {'object': 'can', 'category': 'Metal'},
                            lambda **state: self.events.append(state), lambda cmd: None, self.board,
                            camera_reset=lambda: self.resets.append('reset'))
        p.cancelled = FastEvent()
        self.config = dict(ip='drive', gear_ratio_x=4, gear_ratio_y=4,
                           rail=validate({'settle_s': 0, 'taught': dict(center=True, plus=True, minus=True)}))
        return p

    def test_capture_timeout_then_next_run_succeeds_without_stuck_estop(self):
        p = self.make_pipeline()
        def missing(*args): raise RuntimeError('capture timeout')
        p.inbox.capture = missing
        p.reserve(); p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertFalse(self.board.state['stopped'])
        self.assertNotIn('x', self.board.commands)
        self.assertEqual(len(self.resets), 2, 'one reset before retry, one after final failure')
        self.assertTrue(self.events[-1]['retry_ready'])
        p.inbox.capture = lambda *args: b'new'
        self.assertTrue(p.reserve())
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete', self.events[-1])

    def test_capture_retries_only_once_before_moving_tray(self):
        p = self.make_pipeline()
        calls = []
        def capture(*args):
            calls.append('capture')
            if len(calls) == 1: raise RuntimeError('camera timeout')
            return b'new'
        p.inbox.capture = capture
        p.reserve(); p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete')
        self.assertEqual(calls, ['capture', 'capture'])
        self.assertEqual(self.resets, ['reset'])

    def test_explicit_reset_homes_without_changing_zero_and_clears_reservation(self):
        p = self.make_pipeline()
        self.board.state.update(stopped=True, x=30, y=-30)
        def transport(ip, cmd):
            if cmd == 'home':
                self.board.commands.append(cmd)
                self.board.state.update(stopped=False, pid=False, busy=False, x=0, y=0)
                return 'ACK:OK'
            return self.board(ip, cmd)
        p.transport = transport
        self.assertTrue(p.reserve_reset())
        self.assertFalse(p.reserve())
        p.reset('drive')
        self.assertEqual(self.events[-1]['step'], 'idle')
        self.assertIn('home', self.board.commands)
        self.assertNotIn('zero', self.board.commands)
        self.assertFalse(p.resetting)
        self.assertTrue(p.reserve())

    def test_cancel_does_not_start_automatic_camera_recovery(self):
        p = self.make_pipeline()
        p.reserve(); p.cancelled.set(); p.run(self.config)
        self.assertEqual(self.resets, [])
        self.assertFalse(p.active)


class ResetRouteTests(unittest.TestCase):
    def test_confirmed_xy_home_does_not_zero_the_rail_estimate(self):
        from copy import deepcopy
        from test_sorting_integration import load_function
        events = []
        state = dict(mx_steps=50, my_steps=-50, mz_steps=300, mx_deg=90, my_deg=-90,
                     mz_deg=540, roll_deg=90, yaw_deg=0)
        update = load_function('update_motor_state', {'motor_state': state, 'drive_settings': {},
                              'deepcopy': deepcopy, 'broadcast_event': events.append})
        update('reset_xy')
        self.assertEqual((state['mx_steps'], state['my_steps'], state['roll_deg']), (0, 0, 0))
        self.assertEqual((state['mz_steps'], state['mz_deg']), (300, 540))
        self.assertEqual(events[-1]['state']['mz_steps'], 300)

    def setUp(self):
        from test_sorting_integration import load_function
        self.events, self.jobs = [], []
        self.p = SortingPipeline(FrameInbox(), lambda: None, lambda f: None,
                                 lambda **s: None, lambda c: None, lambda *a: 'ACK:OK')
        self.env = {'sorting_pipeline': self.p, 'rail_tools': SimpleNamespace(busy=False),
                    'drive_ip': lambda: 'drive', 'board_settings': SimpleNamespace(accepts=lambda *a: True),
                    'get_jc1060_ip': lambda: 'jc', 'camera_stream': SimpleNamespace(stop=lambda: None),
                    'CONFIG': {'imagesendermcu': {'command': 'capture'}},
                    'jc1060_drive_input': SimpleNamespace(cancel=lambda: None),
                    'publish_sorting_state': lambda **s: self.events.append(s), 'jsonify': lambda value: value,
                    'threading': SimpleNamespace(Thread=lambda **kw: SimpleNamespace(start=lambda: self.jobs.append(kw)))}
        self.call = load_function('reset_sorting_sequence', self.env)

    def test_reset_reserves_all_controls_until_worker_finishes(self):
        response = self.call()
        self.assertEqual(response['status'], 'resetting')
        self.assertTrue(self.p.active)
        self.assertTrue(self.p.resetting)
        self.assertFalse(self.p.reserve())
        self.assertIsNone(self.env['CONFIG']['imagesendermcu']['command'])
        self.assertEqual(len(self.jobs), 1)

    def test_active_sorting_is_not_force_unlocked(self):
        self.p.reserve()
        response, code = self.call()
        self.assertEqual(code, 409)
        self.assertEqual(self.jobs, [])
        self.assertTrue(self.p.active)

    def test_failed_worker_start_releases_both_reservations(self):
        def fail(): raise RuntimeError('thread unavailable')
        self.env['threading'] = SimpleNamespace(Thread=lambda **kw: SimpleNamespace(start=fail))
        with self.assertRaises(RuntimeError): self.call()
        self.assertFalse(self.p.active)
        self.assertFalse(self.p.resetting)


if __name__ == '__main__': unittest.main()
