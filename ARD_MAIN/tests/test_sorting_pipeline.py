"""Offline sorting tests. No network, AI requests or physical motion."""
import json
from pathlib import Path
import sys
import threading
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core.sorting_pipeline import FrameInbox, SortingPipeline, validate_analysis

class FastEvent(threading.Event):
    def wait(self, timeout=None):
        return self.is_set()

class FakeMCU:
    def __init__(self):
        self.state = dict(protocol=1, boot_id=42, busy=False, stopped=False,
                          pid=False, x=0, y=0, z=0, steps_per_mm=43.48, tof_mm=145)
        self.commands = []
        self.drop_move = False
        self.old_firmware = False
        self.reject = False
        self.pending = 0
    def __call__(self, ip, cmd):
        if cmd == 'status':
            if self.old_firmware:
                return 'ACK:OK'
            reply = self.state.copy()
            if self.pending:
                reply['busy'] = True
                self.pending -= 1
            return json.dumps(reply)
        self.commands.append(cmd)
        if cmd == 'x':
            self.state['stopped'] = True
            return 'ACK:OK'
        if self.reject:
            return 'ERR:REJECTED'
        if not self.drop_move:
            if cmd.startswith('mzr'):
                self.state['z'] += int(cmd[3:])
            elif cmd.startswith('b'):
                x,y = map(int, cmd[1:].split(','))
                self.state['x'] += x
                self.state['y'] += y
        self.pending = 1
        return 'ACK:OK'

class SortingTests(unittest.TestCase):
    def build(self, category='Metal', analyze=None):
        self.inbox, self.mcu = FrameInbox(), FakeMCU()
        self.events, self.updates = [], []
        self.config = dict(ip='test-mcu', gear_ratio_x=4, gear_ratio_y=4)
        self.pipeline = SortingPipeline(self.inbox, lambda: self.inbox.publish(b'new'),
            analyze or (lambda frame: {'object':'test object', 'category':category}),
            lambda **changes: self.events.append(changes), self.updates.append, self.mcu)
        self.pipeline.cancelled = FastEvent()
        self.assertTrue(self.pipeline.reserve())
        return self.pipeline

    def test_all_destinations_complete_after_confirmed_return(self):
        for category, yaw, rail in [('Metal',90,652),('Glass',90,-652),('Others',90,-652),('Paper',270,652),('Plastic',270,-652)]:
            with self.subTest(category=category):
                p=self.build(category)
                p.run(self.config)
                self.assertEqual(self.events[-1]['step'], 'complete')
                self.assertEqual(self.mcu.commands[0], f'mzr{rail}')
                self.assertEqual(self.mcu.commands[1], f'b{round(yaw/1.8*4)},{round(yaw/1.8*4)}')
                self.assertEqual(self.mcu.commands[-1], f'mzr{-rail}')
                self.assertEqual([self.mcu.state[a] for a in 'xyz'], [0,0,0])
                self.assertFalse(p.active)
                self.assertEqual(len(self.updates),6)

    def test_analysis_failure_never_moves(self):
        p=self.build(analyze=lambda frame: {'object':'bad','category':'Unrecognized'})
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(self.mcu.commands, [])

    def test_missing_completion_stops_and_never_dumps(self):
        p=self.build(); self.mcu.drop_move=True
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(self.mcu.commands, ['mzr652','x'])
        self.assertEqual(self.updates, [])

    def test_reboot_between_moves_stops_before_next_command(self):
        p=self.build()
        def update(command):
            self.updates.append(command)
            self.mcu.state['boot_id'] += 1
        p.motor_update=update
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertEqual(self.mcu.commands,['mzr652','x'])

    def test_rejected_command_stops_without_advancing_pose(self):
        p=self.build(); self.mcu.reject=True
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertEqual(self.updates,[])

    def test_old_firmware_requires_update(self):
        p=self.build(); self.mcu.old_firmware=True
        p.run(self.config)
        self.assertIn('펌웨어',self.events[-1]['step_desc'])
        self.assertEqual(self.mcu.commands, [])

    def test_pid_and_non_home_preconditions(self):
        for field,value in [('pid',True),('x',20)]:
            p=self.build(); self.mcu.state[field]=value
            p.run(self.config)
            self.assertEqual(self.events[-1]['step'],'error')
            self.assertEqual(self.mcu.commands,[])

    def test_open_loop_ignores_broken_tof_and_returns_to_start_steps(self):
        p=self.build(); self.mcu.state['tof_mm']=0; self.mcu.state['z']=400
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'complete')
        self.assertEqual(self.mcu.state['z'],400)
        self.assertEqual(self.events[-1]['current_rail_z'],0)

    def test_concurrent_reservation_and_cancel(self):
        p=self.build()
        self.assertFalse(p.reserve())
        p.cancel()
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertFalse(p.active)

    def test_new_frame_same_length_is_accepted(self):
        inbox=FrameInbox(); inbox.publish(b'old')
        frame=inbox.capture(lambda: inbox.publish(b'new'),threading.Event(),timeout=.01)
        self.assertEqual(frame,b'new')

    def test_cached_frame_is_not_accepted(self):
        inbox=FrameInbox(); inbox.publish(b'old')
        with self.assertRaises(RuntimeError):
            inbox.capture(lambda: None,threading.Event(),timeout=.01)

    def test_model_exception_stops_before_motion(self):
        def failure(frame): raise RuntimeError('AI unavailable')
        p=self.build(analyze=failure); p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertEqual(self.mcu.commands,[])

    def test_phase_names_each_motion_before_it_runs(self):
        # Remote screens (JC1060 Main tab) animate the axis named by phase.
        p=self.build(); p.run(self.config)
        phases=[e['phase'] for e in self.events if 'phase' in e]
        self.assertEqual(phases,['capture','analyze','rail_out','yaw_out','roll_out','settle',
                                 'roll_back','yaw_back','rail_back','done'])
        p=self.build(analyze=lambda frame: {'object':'bad','category':'Unrecognized'}); p.run(self.config)
        self.assertEqual(self.events[-1]['phase'],'error')

if __name__=='__main__': unittest.main()
