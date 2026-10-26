"""Offline Z rail PID tests: calibration, direction measurement, sorting with ToF targets."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core import rail_control
from core.rail_control import RailTools, validate
from core.sorting_pipeline import FrameInbox, SortingPipeline


class FastEvent(threading.Event):
    def wait(self, timeout=None):
        return self.is_set()


class Clock:
    def __init__(self):
        self.t = 0.

    def __call__(self):
        self.t += .1
        return self.t


class RailBoard:
    """DriveMCU with a mirrored rail: distance = base + dir * z / spm. PID lands on target."""

    def __init__(self, true_dir=-1, spm=40.0, base=160.0):
        self.true_dir, self.spm, self.base = true_dir, spm, base
        self.state = dict(protocol=1, boot_id=7, busy=False, stopped=False, pid=False, jogging=False,
                          x=0, y=0, z=0, steps_per_mm=43.48, tof_ok=True, tof_status='OK',
                          rail_target=140, rail_dir=-1, rail_settled=False, pid_p=.3, pid_i=.01, pid_d=5.)
        self.commands = []
        self.runaway = False
        self.pid_polls = 0

    def tof(self):
        return self.base + self.true_dir * self.state['z'] / self.spm

    def __call__(self, ip, cmd):
        if cmd == 'status':
            s = self.state
            if s['pid']:
                self.pid_polls += 1
                if self.runaway:
                    s['pid'] = False
                elif self.pid_polls >= 2:  # converged: move z so the ToF equals the target
                    s['z'] = round((s['rail_target'] - self.base) * self.spm / self.true_dir)
                    s['rail_settled'] = True
            return json.dumps(dict(s, tof_mm=self.tof()))
        self.commands.append(cmd)
        s = self.state
        if cmd == 'x':
            s.update(stopped=True, pid=False, rail_settled=False)
        elif cmd.startswith('mzr'):
            s['z'] += int(cmd[3:])
        elif cmd.startswith('mzt'):
            s['rail_target'] = int(cmd[3:])
            s['rail_settled'] = False
            self.pid_polls = 0
        elif cmd.startswith('mzm'):
            s['pid'] = cmd == 'mzm1'
            s['rail_settled'] = False
            self.pid_polls = 0
        elif cmd.startswith('mzv'):
            if s['pid']:
                return 'ERR:REJECTED'
            s['rail_dir'] = int(cmd[3:])
        elif cmd.startswith('mzs'):
            s['steps_per_mm'] = float(cmd[3:])
        elif cmd[:3] in ('mzp', 'mzi', 'mzd'):
            s['pid_' + cmd[2]] = float(cmd[3:])
        elif cmd.startswith('b'):
            x, y = map(int, cmd[1:].split(','))
            s['x'] += x
            s['y'] += y
        elif cmd == 'home':  # X/Y to 0, Z kept, PID off
            s.update(x=0, y=0, pid=False, rail_settled=False)
        return 'ACK:OK'


def tools_for(board):
    folder = tempfile.mkdtemp()
    tools = RailTools(Path(folder) / 'rail.json', board, clock=Clock())
    tools.cancelled = FastEvent()
    # These fixtures use the original rig's distances (140..180 mm); the 70..120 mm safety
    # limits have their own tests below.
    tools.update({'rail_min_mm': 1, 'rail_max_mm': 2000})
    return tools


class RailCalibrationTests(unittest.TestCase):
    def test_pid_apply_readback_then_save(self):
        board = RailBoard()
        tools = tools_for(board)
        gains = dict(pid_p=.12, pid_i=0, pid_d=0)
        self.assertEqual(tools.run(tools.apply_pid, 'ip', gains), gains)
        self.assertEqual(board.commands, ['mzp0.12', 'mzi0', 'mzd0'])
        saved = RailTools(tools.path, board).snapshot()
        self.assertTrue(all(saved[k] == v for k, v in gains.items()))

    def test_pid_apply_refuses_busy_or_enabled(self):
        for flag in ('busy', 'pid', 'jogging'):
            board = RailBoard()
            board.state[flag] = True
            tools = tools_for(board)
            with self.subTest(flag=flag), self.assertRaises(RuntimeError):
                tools.run(tools.apply_pid, 'ip', dict(pid_p=.12, pid_i=0, pid_d=0))
            self.assertEqual(board.commands, [])

    def test_pid_readback_failure_does_not_persist(self):
        board = RailBoard()
        def transport(ip, cmd):
            if cmd.startswith('mzp'):
                return 'ACK:OK'  # command lost despite ACK
            return board(ip, cmd)
        tools = tools_for(board)
        tools.transport = transport
        before = tools.snapshot()
        saved = tools.path.read_text(encoding='utf-8')
        with self.assertRaises(RuntimeError):
            tools.run(tools.apply_pid, 'ip', dict(pid_p=.12, pid_i=0, pid_d=0))
        self.assertEqual(tools.snapshot(), before)
        self.assertEqual(tools.path.read_text(encoding='utf-8'), saved)

    def test_pid_rejects_invalid_inputs_before_commands(self):
        for changes in ({'pid_p': .12}, dict(pid_p=float('nan'), pid_i=0, pid_d=0),
                        dict(pid_p=True, pid_i=0, pid_d=0), dict(pid_p=-1, pid_i=0, pid_d=0)):
            board = RailBoard()
            tools = tools_for(board)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                tools.run(tools.apply_pid, 'ip', changes)
            self.assertEqual(board.commands, [])

    def test_defaults_mark_mirrored_mount_untaught(self):
        c = validate({})
        self.assertEqual(c['rail_dir'], -1)
        self.assertEqual(c['mode'], 'pid')
        self.assertFalse(any(c['taught'].values()))
        self.assertLess(c['plus_mm'], c['center_mm'])  # +15 mm side is now the shorter distance

    def test_validation_rejects_bad_values(self):
        for bad in ({'rail_dir': 0}, {'rail_dir': True}, {'center_mm': 0}, {'mode': 'x'},
                    {'steps_per_mm': -1}, {'unknown': 1}, {'taught': {'center': 1}}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate(bad)

    def test_config_round_trip(self):
        board = RailBoard()
        tools = tools_for(board)
        tools.update({'center_mm': 150, 'taught': {'center': True}})
        again = RailTools(tools.path, board)
        self.assertEqual(again.snapshot()['center_mm'], 150)
        self.assertTrue(again.snapshot()['taught']['center'])

    def test_direction_measurement_finds_sign_and_scale_and_returns(self):
        for true_dir in (1, -1):
            with self.subTest(true_dir=true_dir):
                board = RailBoard(true_dir=true_dir, spm=40.0)
                tools = tools_for(board)
                r = tools.run(tools.measure_direction, 'ip', 400, True)
                self.assertEqual(r['rail_dir'], true_dir)
                self.assertAlmostEqual(r['steps_per_mm'], 40.0, places=2)
                self.assertEqual(board.state['z'], 0)
                self.assertIn(f'mzv{true_dir}', board.commands)
                self.assertEqual(tools.snapshot()['rail_dir'], true_dir)

    def test_direction_measurement_refuses_while_pid_on(self):
        board = RailBoard(); board.state['pid'] = True
        tools = tools_for(board)
        with self.assertRaises(RuntimeError):
            tools.run(tools.measure_direction, 'ip', 400, False)
        self.assertEqual(board.commands, [])

    def test_teach_and_opposite_side_check(self):
        board = RailBoard(true_dir=-1, spm=40.0, base=158.0)
        tools = tools_for(board)
        tools.run(tools.teach, 'ip', 'center')
        board.state['z'] = 600            # mirrored: +steps shorten the distance
        tools.run(tools.teach, 'ip', 'plus')
        board.state['z'] = -600
        tools.run(tools.teach, 'ip', 'minus')
        c = tools.snapshot()
        self.assertEqual((c['center_mm'], c['plus_mm'], c['minus_mm']), (158.0, 143.0, 173.0))
        board.state['z'] = 300            # same side as plus: rejected, plus stays
        tools.update({'taught': {'minus': True}})
        with self.assertRaises(RuntimeError):
            tools.run(tools.teach, 'ip', 'minus')
        self.assertEqual(tools.snapshot()['minus_mm'], 173.0)

    def test_goto_waits_for_settled_and_holds(self):
        board = RailBoard()
        tools = tools_for(board)
        r = tools.run(tools.goto, 'ip', 150, True)
        self.assertEqual(r['target_mm'], 150)
        self.assertAlmostEqual(r['tof_mm'], 150, places=1)
        self.assertTrue(board.state['pid'])  # hold
        self.assertEqual(board.commands[:2], ['mzt150', 'mzm1'])

    def test_runaway_pid_reports_direction_problem(self):
        board = RailBoard(); board.runaway = True
        tools = tools_for(board)
        with self.assertRaises(RuntimeError) as ctx:
            tools.run(tools.goto, 'ip', 150, True)
        self.assertIn('방향', str(ctx.exception))

    def test_goto_limits_targets_to_taught_span(self):
        tools = tools_for(RailBoard())
        tools.update({'center_mm': 160, 'plus_mm': 145, 'minus_mm': 175,
                      'taught': {'center': True, 'plus': True, 'minus': True}})
        self.assertEqual(tools.target_of(mm=184), 184)
        with self.assertRaises(ValueError):
            tools.target_of(mm=190)

    def test_sweep_summary(self):
        board = RailBoard()
        tools = tools_for(board)
        tools.update({'center_mm': 160, 'plus_mm': 145, 'minus_mm': 175, 'settle_s': 0,
                      'taught': {'center': True, 'plus': True, 'minus': True}})
        r = tools.run(tools.sweep, 'ip', 2)
        self.assertEqual([row['slot'] for row in r['rows']],
                         ['center', 'plus', 'center', 'minus', 'center', 'plus', 'center', 'minus', 'center'])
        self.assertEqual(r['summary']['plus']['target_mm'], 145)
        self.assertEqual(r['summary']['center']['step_spread'], 0)
        self.assertFalse(board.state['pid'])

    def test_one_tool_at_a_time(self):
        tools = tools_for(RailBoard())
        tools.running.acquire()
        with self.assertRaises(BlockingIOError):
            tools.run(tools.sample_tof, 'ip', 1)


class PidSortingTests(unittest.TestCase):
    def test_failed_camera_position_never_captures(self):
        p = self.build()
        self.board.runaway = True
        frames = []
        p.inbox.capture = lambda *args: frames.append('unexpected')
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(frames, [])

    def test_capture_only_after_120mm_settles_and_return_to_120(self):
        p = self.build('Metal')
        rail = self.config['rail']
        rail.update(center_mm=95, plus_mm=70, minus_mm=120, capture_mm=120)
        captures = []
        def capture(trigger, cancelled):
            captures.append(self.board.tof())
            self.assertFalse(self.board.state['pid'])
            self.assertTrue(self.board.state['rail_settled'] or self.board.tof() == 120)
            return b'new frame'
        p.inbox.capture = capture
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete', self.events[-1])
        self.assertEqual(captures, [120])
        self.assertEqual(self.targets(), ['mzt120', 'mzt70', 'mzt120'])
        self.assertAlmostEqual(self.board.tof(), 120)

    def build(self, category='Metal', taught=True):
        self.board = RailBoard()
        self.events = []
        self.pipeline = SortingPipeline(FrameInbox(), None, lambda f: {'object': 'can', 'category': category},
                                        lambda **c: self.events.append(c), lambda cmd: None, self.board)
        self.pipeline.inbox.capture = lambda trigger, cancelled: b'frame'
        self.pipeline.cancelled = FastEvent()
        rail = validate({'center_mm': 158, 'plus_mm': 143, 'minus_mm': 173, 'settle_s': 0,
                         'rail_min_mm': 1, 'rail_max_mm': 2000,
                         'taught': {s: taught for s in rail_control.SLOTS}})
        self.config = dict(ip='mcu', gear_ratio_x=4, gear_ratio_y=4, rail=rail)
        self.assertTrue(self.pipeline.reserve())
        return self.pipeline

    def targets(self):
        return [c for c in self.board.commands if c.startswith('mzt')]

    def test_bins_use_taught_tof_targets(self):
        for category, target in [('Metal', 143), ('Paper', 143), ('Plastic', 173), ('Glass', 173)]:
            with self.subTest(category=category):
                p = self.build(category)
                p.run(self.config)
                self.assertEqual(self.events[-1]['step'], 'complete', self.events[-1])
                self.assertEqual(self.targets(), ['mzt120', f'mzt{target}', 'mzt120'])
                self.assertFalse(any(c.startswith('mzr') for c in self.board.commands))
                self.assertEqual(self.board.commands[0], 'mzv-1')  # calibration pushed first
                self.assertFalse(self.board.state['pid'])
                self.assertEqual([self.board.state[a] for a in 'xy'], [0, 0])

    def test_pid_is_off_during_tray_moves(self):
        p = self.build()
        p.run(self.config)
        cmds = self.board.commands
        first_tray = next(i for i, c in enumerate(cmds) if c.startswith('b'))
        self.assertEqual(cmds[first_tray - 1], 'mzm0')

    def test_step1_disc_level_then_rail_home_before_capture(self):
        p = self.build()
        self.board.state.update(x=40, y=-40)
        captured = []
        def capture(trigger, cancelled):
            captured.append(list(self.board.commands))
            return b'frame'
        p.inbox.capture = capture
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete', self.events[-1])
        before = captured[0]
        self.assertEqual(before[0], 'home')
        self.assertEqual(before[-3:], ['mzt120', 'mzm1', 'mzm0'])
        descs = [e.get('step_desc') for e in self.events]
        self.assertIn('원점 복귀 완료 (디스크 수평 · 레일 ToF 120mm)', descs)
        self.assertEqual(self.events[0]['step_num'], 1)

    def test_step1_pid_failure_stops_before_capture(self):
        p = self.build()
        self.board.state.update(x=40)
        self.board.runaway = True
        frames = []
        p.inbox.capture = lambda *args: frames.append(1)
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(self.board.commands[-1], 'x')
        self.assertEqual(frames, [])

    def test_home_needs_no_teaching_and_return_goes_home(self):
        p = self.build(taught=False)
        self.config['rail'] = validate({'mode': 'open_loop', 'settle_s': 0})  # nothing taught
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete', self.events[-1])
        self.assertEqual(self.targets(), ['mzt120', 'mzt120'])  # origin at start and at the end
        mzr = [c for c in self.board.commands if c.startswith('mzr')]
        self.assertEqual(mzr, ['mzr2174'])  # origin 120 -> +side 70 mm by steps; the return is the ToF origin

    def test_pid_mode_needs_only_the_bin_slots_taught(self):
        p = self.build(taught=False)
        rail = self.config['rail']
        rail['taught'] = {'center': False, 'plus': True, 'minus': True}
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete', self.events[-1])
        rail['destinations']['Metal'] = {'rail': 'center', 'tilt': 'forward'}
        p = self.build(taught=False)
        self.config['rail']['taught'] = {'center': False, 'plus': True, 'minus': True}
        self.config['rail']['destinations']['Metal'] = {'rail': 'center', 'tilt': 'forward'}
        p.run(self.config)
        self.assertIn('중앙', self.events[-1]['step_desc'])
        self.assertEqual(self.board.commands, [])

    def test_untaught_rail_never_moves(self):
        p = self.build(taught=False)
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertIn('티칭', self.events[-1]['step_desc'])
        self.assertEqual(self.board.commands, [])

    def test_runaway_stops_everything(self):
        p = self.build()
        self.board.runaway = True
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'error')
        self.assertEqual(self.board.commands[-1], 'x')
        self.assertFalse(any(c.startswith('b') for c in self.board.commands))

    def test_open_loop_mode_still_uses_steps(self):
        p = self.build()
        self.config['rail'] = validate({'mode': 'open_loop', 'settle_s': 0})
        p.run(self.config)
        self.assertEqual(self.events[-1]['step'], 'complete')
        self.assertIn('mzr2174', self.board.commands)
        self.assertEqual(self.targets(), ['mzt120', 'mzt120'])


class DestinationTableTests(unittest.TestCase):
    def taught_tools(self, board, **changes):
        tools = tools_for(board)
        tools.update({'center_mm': 158, 'plus_mm': 143, 'minus_mm': 173, 'settle_s': 0,
                      'taught': {s: True for s in rail_control.SLOTS}, **changes})
        return tools

    def test_default_table_and_validation(self):
        c = validate({})
        self.assertEqual(c['destinations']['Metal'], {'rail': 'plus', 'tilt': 'forward'})
        self.assertEqual(c['destinations']['Plastic'], {'rail': 'minus', 'tilt': 'backward'})
        self.assertEqual(c['tilt_deg'], 45)
        for bad in ({'destinations': {'Wood': {'rail': 'plus'}}},
                    {'destinations': {'Metal': {'rail': 'left'}}},
                    {'destinations': {'Metal': {'tilt': 'sideways'}}},
                    {'destinations': {'Metal': {'yaw': 90}}},
                    {'destinations': []}, {'tilt_deg': 120}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate(bad)
        with self.assertRaises(ValueError):
            validate({'tilt_deg': 61})  # disc tilt safety limit ±60°
        self.assertEqual(validate({'tilt_deg': 60})['tilt_deg'], 60)
        edited = validate({'destinations': {'Paper': {'tilt': 'forward'}}})
        self.assertEqual(edited['destinations']['Paper'], {'rail': 'plus', 'tilt': 'forward'})
        self.assertEqual(edited['destinations']['Metal'], {'rail': 'plus', 'tilt': 'forward'})

    def test_drop_pid_rail_then_tilt_then_tilt_back_before_rail_home(self):
        board = RailBoard()
        tools = self.taught_tools(board, destinations={'Glass': {'rail': 'minus', 'tilt': 'backward'}})
        r = tools.run(tools.test_drop, 'ip', 'Glass', (4, 4))
        cmds = [c for c in board.commands if c.startswith(('mzt', 'b'))]
        self.assertEqual(cmds, ['mzt173', 'b-100,100', 'b100,-100', 'mzt120'])  # back to home_mm
        self.assertEqual(r['home_mm'], 120)
        self.assertEqual(r['tilt'], 'backward')
        self.assertFalse(board.state['pid'])
        self.assertEqual([board.state[a] for a in 'xy'], [0, 0])

    def test_drop_refuses_off_origin_and_busy_tool(self):
        board = RailBoard(); board.state['x'] = 30
        tools = self.taught_tools(board)
        with self.assertRaises(RuntimeError):
            tools.run(tools.test_drop, 'ip', 'Metal', (4, 4))
        self.assertEqual(board.commands, [])
        tools.running.acquire()
        with self.assertRaises(BlockingIOError):
            tools.run(tools.test_drop, 'ip', 'Metal', (4, 4))
        with self.assertRaises(ValueError):
            tools_for(RailBoard()).run(tools.test_drop, 'ip', 'Wood', (4, 4))

    def test_drop_open_loop_steps_then_tof_home(self):
        board = RailBoard()
        tools = tools_for(board)
        tools.update({'mode': 'open_loop'})  # untaught: the origin needs no teaching
        tools.run(tools.test_drop, 'ip', 'Paper', (4, 4))
        cmds = [c for c in board.commands if c.startswith(('mzr', 'mzt', 'b'))]
        # Paper -> +side 70 mm: (70 - 120) x 43.48 steps/mm x rail_dir -1 = +2174 steps from the origin.
        self.assertEqual(cmds, ['mzr2174', 'b-100,100', 'b100,-100', 'mzt120'])

    def test_drop_open_loop_without_tof_steps_back(self):
        board = RailBoard(); board.state['tof_ok'] = False
        tools = tools_for(board)
        tools.update({'mode': 'open_loop'})
        tools.run(tools.test_drop, 'ip', 'Paper', (4, 4))
        self.assertEqual(board.commands, ['mzr2174', 'b-100,100', 'b100,-100', 'mzr-2174'])

    def test_home_setting_alias_and_independence(self):
        c = validate({})
        self.assertEqual((c['home_mm'], c['capture_mm']), (120, 120))
        self.assertEqual(validate({'capture_mm': 110})['home_mm'], 110)  # older name
        self.assertEqual(validate({'home_mm': 110, 'capture_mm': 100})['capture_mm'], 110)
        # The user's saved positions: minus 120 = home 120 is fine, home is not part of teaching.
        saved = validate({'center_mm': 95, 'plus_mm': 70, 'minus_mm': 120, 'capture_mm': 120,
                          'taught': {s: True for s in rail_control.SLOTS}})
        self.assertEqual(saved['home_mm'], 120)
        for bad in (0, 2001, 'x', True):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate({'home_mm': bad})
        tools = tools_for(RailBoard())
        self.assertEqual(tools.target_of(slot='home'), 120)

    def test_teach_home_saves_current_tof(self):
        board = RailBoard(base=118.0)
        tools = tools_for(board)
        r = tools.run(tools.teach, 'ip', 'home')
        self.assertEqual(r['config']['home_mm'], 118.0)
        self.assertEqual(tools.snapshot()['capture_mm'], 118.0)
        self.assertFalse(any(tools.snapshot()['taught'].values()))

    def test_route_refuses_while_sorting_and_runs_with_lock(self):
        from flask import Flask
        from api.rail_routes import register_rail_routes
        board = RailBoard()
        tools = self.taught_tools(board)
        sorting = {'active': True}
        app = Flask(__name__)
        register_rail_routes(app, tools, lambda: 'ip', lambda: sorting['active'], get_ratios=lambda: (4, 4))
        client = app.test_client()
        self.assertEqual(client.post('/api/rail/test-drop', json={'category': 'Metal'}).status_code, 409)
        self.assertEqual(board.commands, [])
        sorting['active'] = False
        tools.running.acquire()
        self.assertEqual(client.post('/api/rail/test-drop', json={'category': 'Metal'}).status_code, 409)
        tools.running.release()
        response = client.post('/api/rail/test-drop', json={'category': 'Metal'})
        self.assertEqual(response.status_code, 200, response.json)
        self.assertEqual(response.json['result']['tilt_steps'], [100, -100])
        self.assertFalse(tools.busy)


class SafetyLimitTests(unittest.TestCase):
    """Z safety limits ToF 70..120 mm (user spec 2026-10-27)."""
    def test_defaults_and_user_values_inside(self):
        c = validate({'center_mm': 95, 'plus_mm': 70, 'minus_mm': 120, 'capture_mm': 120})
        self.assertEqual((c['rail_min_mm'], c['rail_max_mm'], c['home_mm']), (70, 120, 120))
        self.assertIn('mzl70,120', rail_control.parameter_commands(c))

    def test_validation_rejects_outside_and_bad_limits(self):
        for bad in ({'home_mm': 130}, {'center_mm': 60}, {'plus_mm': 125}, {'rail_min_mm': 115},
                    {'rail_min_mm': 100, 'rail_max_mm': 105}, {'rail_max_mm': 3000}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate(bad)
        wide = validate({'rail_min_mm': 60, 'rail_max_mm': 130, 'home_mm': 125})
        self.assertIn('mzl60,130', rail_control.parameter_commands(wide))

    def test_open_loop_steps_from_origin(self):
        c = validate({})
        self.assertEqual(rail_control.open_loop_steps(c, 70, 120, 43.48), 2174)    # mirrored rail_dir -1
        self.assertEqual(rail_control.open_loop_steps(c, 120, 120, 43.48), 0)
        c1 = validate({'rail_dir': 1})
        self.assertEqual(rail_control.open_loop_steps(c1, 70, 120, 43.48), -2174)
        with self.assertRaises(RuntimeError):
            rail_control.open_loop_steps(c, 60, 120, 43.48)

    def test_goto_teach_and_pid_refuse_outside(self):
        board = RailBoard(base=125.0)  # the rail sits outside the range
        tools = RailTools(Path(tempfile.mkdtemp()) / 'rail.json', board, clock=Clock())
        tools.cancelled = FastEvent()
        with self.assertRaises(ValueError):
            tools.target_of(mm=130)
        self.assertEqual(tools.target_of(mm=120), 120)
        with self.assertRaises(ValueError):
            tools.run(tools.teach, 'ip', 'home')  # 125 mm cannot become the origin
        with self.assertRaises(RuntimeError):
            rail_control.pid_move(lambda cmd: None, lambda: {}, 140, tools.snapshot(), FastEvent())
        self.assertEqual(board.commands, [])

    def test_sorting_reports_a_limit_stop_clearly(self):
        board = RailBoard()
        original = board.__call__
        def transport(ip, cmd):
            reply = original(ip, cmd)
            if cmd.startswith('mzr'):  # the board's guard stopped this rail move early
                board.state['z'] -= int(cmd[3:]) // 2
                board.state['rail_limit_count'] = board.state.get('rail_limit_count', 0) + 1
                board.state['rail_limit'] = 'Z stopped at ToF 71.0 mm near the min limit'
            return reply
        board.state['tof_ok'] = False  # open loop without the ToF origin: rail move by steps
        events = []
        p = SortingPipeline(FrameInbox(), None, lambda f: {'object': 'can', 'category': 'Metal'},
                            lambda **c: events.append(c), lambda cmd: None, transport)
        p.inbox.capture = lambda trigger, cancelled: b'frame'
        p.cancelled = FastEvent()
        self.assertTrue(p.reserve())
        p.run(dict(ip='mcu', gear_ratio_x=4, gear_ratio_y=4, rail=validate({'mode': 'open_loop'})))
        self.assertEqual(events[-1]['step'], 'error')
        self.assertIn('안전 한계', events[-1]['error'])
        self.assertIn('min limit', events[-1]['error'])
        self.assertFalse(any(c.startswith('b') for c in board.commands), 'no tilt after a limit stop')


if __name__ == '__main__':
    unittest.main()
