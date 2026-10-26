"""Offline sorting tests. No network, AI requests or physical motion."""
import json
from pathlib import Path
import sys
import threading
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core.sorting_pipeline import FrameInbox, SortingPipeline, validate_analysis
from core.rail_control import validate as rail_validate

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
        self.rejected = set()
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
        if self.reject or cmd in self.rejected:
            return 'ERR:REJECTED'
        if not self.drop_move:
            if cmd == 'home':
                self.state.update(x=0, y=0, pid=False)
            elif cmd.startswith('mzr'):
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

    def test_all_destinations_use_rail_and_tilt_only(self):
        # Default table: Metal +/fwd, Glass & Others -/fwd, Paper +/back, Plastic -/back. 45 deg x 4 = 100 steps.
        # Open loop from the origin (ToF 120): +side 70 mm = (70-120) x 43.48 x rail_dir -1 = 2174 steps;
        # -side 120 mm is the origin itself, so no rail move.
        for category, rail, tilt in [('Metal',2174,1),('Glass',0,1),('Others',0,1),('Paper',2174,-1),('Plastic',0,-1)]:
            with self.subTest(category=category):
                p=self.build(category)
                p.run(self.config)
                self.assertEqual(self.events[-1]['step'], 'complete')
                tilts=[f'b{100*tilt},{-100*tilt}', f'b{-100*tilt},{100*tilt}']
                self.assertEqual(self.mcu.commands, [f'mzr{rail}', *tilts, f'mzr{-rail}'] if rail else tilts)
                self.assertEqual([self.mcu.state[a] for a in 'xyz'], [0,0,0])
                self.assertFalse(p.active)
                self.assertEqual(len(self.updates),4 if rail else 2)
                dump=[e for e in self.events if e.get('phase')=='roll_out'][0]
                self.assertEqual(dump['step_num'],5)

    def test_no_disk_turn_is_ever_sent(self):
        for category in ('Metal','Glass','Others','Paper','Plastic'):
            p=self.build(category); p.run(self.config)
            for cmd in self.mcu.commands:
                self.assertNotIn(cmd[:2], ('dr','dl'))
                if cmd.startswith('b'):
                    x,y=map(int,cmd[1:].split(','))
                    self.assertEqual(x,-y,f'{cmd} is not a pure tilt')
            self.assertTrue(all(e.get('target_yaw',0)==0 and e.get('current_yaw',0)==0 for e in self.events))

    def test_edited_table_changes_the_command_sequence(self):
        p=self.build('Metal')
        self.config['rail']=rail_validate({'mode':'open_loop','tilt_deg':30,
                                           'destinations':{'Metal':{'rail':'center','tilt':'backward'}}})
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'complete')
        self.assertEqual(self.mcu.commands,['mzr1087','b-67,67','b67,-67','mzr-1087'])  # center 95 mm from 120
        self.assertIn('후진',[e.get('step_desc','') for e in self.events if e.get('phase')=='roll_out'][0])

    def test_analysis_failure_never_moves(self):
        p=self.build(analyze=lambda frame: {'object':'bad','category':'Unrecognized'})
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(self.mcu.commands, [])

    def test_missing_completion_stops_and_never_dumps(self):
        p=self.build(); self.mcu.drop_move=True
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(self.mcu.commands, ['mzr2174','x'])
        self.assertEqual(self.updates, [])

    def test_reboot_between_moves_stops_before_next_command(self):
        p=self.build()
        def update(command):
            self.updates.append(command)
            self.mcu.state['boot_id'] += 1
        p.motor_update=update
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertEqual(self.mcu.commands,['mzr2174','x'])

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

    def test_pid_busy_and_jog_preconditions(self):
        for field,value in [('pid',True),('busy',True),('jogging',True),('stopped',True)]:
            p=self.build(); self.mcu.state[field]=value
            p.run(self.config)
            self.assertEqual(self.events[-1]['step'],'error')
            self.assertEqual(self.mcu.commands,[])

    def test_step1_homes_tray_off_origin_then_continues(self):
        p=self.build(); self.mcu.state.update(x=20, y=-7, z=300)
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'complete')
        self.assertEqual(self.mcu.commands[:2],['home','mzr2174'])
        self.assertEqual(self.updates[0],'reset_xy')
        self.assertEqual([self.mcu.state[a] for a in 'xyz'],[0,0,300])  # open loop keeps the rail start
        self.assertEqual([e['step_num'] for e in self.events if 'step_num' in e],[1,2,3,4,5,6,6])
        descs=[e.get('step_desc','') for e in self.events]
        self.assertIn('원점 복귀: 디스크 수평(X/Y 원점) 맞추는 중',descs)
        # No ToF/PID on this board: the rail start stays where it is, with a clear warning.
        self.assertTrue(any('미적용' in d for d in descs))

    def test_step1_home_at_origin_is_not_sent(self):
        p=self.build(); p.run(self.config)
        self.assertNotIn('home',self.mcu.commands)
        self.assertEqual(self.events[0]['step'],'initializing')

    def test_step1_home_rejected_stops_without_other_motion(self):
        p=self.build(); self.mcu.state['x']=20; self.mcu.rejected={'home'}
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertIn('home',self.events[-1]['error'])
        self.assertEqual(self.mcu.commands,['home'])
        self.assertTrue(self.events[-1]['retry_ready'])

    def test_step1_home_not_reached_stops_before_capture(self):
        p=self.build(); self.mcu.state['x']=20; self.mcu.drop_move=True
        captured=[]
        p.trigger=lambda: captured.append(1)
        p.home_timeout=.05
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'],'error')
        self.assertEqual(self.mcu.commands,['home','x'])
        self.assertEqual(captured,[])

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
        self.assertEqual(phases,['init','capture','analyze','rail_out','roll_out','settle',
                                 'roll_back','rail_back','done'])
        p=self.build(analyze=lambda frame: {'object':'bad','category':'Unrecognized'}); p.run(self.config)
        self.assertEqual(self.events[-1]['phase'],'error')

if __name__=='__main__': unittest.main()
