"""Exercise settings, live routes, and command guards without starting AI/audio workers."""
import ast
from copy import deepcopy
from datetime import datetime
import json
from pathlib import Path
import sys
import tempfile
import time
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from flask import Flask, jsonify, request, render_template

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'backend'))
from core.board_settings import BoardSettings, DEFAULTS, DEVICE_NAMES, device_key
from core.stats_manager import StatsManager
from api.board_routes import register_board_routes
from api.jc1060_voice_routes import VoiceIngress

TREE = ast.parse((ROOT / 'backend/app.py').read_text(encoding='utf-8-sig'))


def load_function(name, env):
    node = deepcopy(next(n for n in TREE.body if isinstance(n, ast.FunctionDef) and n.name == name))
    node.decorator_list = []
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(ROOT / 'backend/app.py'), 'exec'), env)
    return env[name]


def literal(name):
    node = next(n for n in TREE.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))
    return ast.literal_eval(node.value)


class BoardSettingsTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT.parent / "tmp"
        scratch.mkdir(exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'boards.json'
        self.boards = BoardSettings(self.path)

    def test_preserve_existing_behavior_without_settings_file(self):
        self.assertEqual(self.boards.snapshot(), DEFAULTS)
        self.assertFalse(self.path.exists())

    def test_saved_selection_survives_restart_and_partial_update(self):
        self.boards.update({'robot3axis_enabled': False, 'control_terminal': 'jc1060'})
        self.boards.update({'voice_device': 'jc1060', 'jc1060_ip': ' 10.0.0.42 '})
        fresh = BoardSettings(self.path)
        self.assertEqual(fresh.snapshot(), {'robot3axis_enabled': False, 'control_terminal': 'jc1060', 'voice_device': 'jc1060', 'jc1060_ip': '10.0.0.42'})
        self.assertFalse(fresh.enabled('robot3axismcu'))

    def test_invalid_values_do_not_mutate_saved_settings(self):
        self.boards.update({'control_terminal': 'jc1060', 'voice_device': 'controlmcu'})
        before = self.path.read_bytes()
        for data in (None, [], {'robot3axis_enabled': 'false'}, {'robot3axis_enabled': 0},
                     {'control_terminal': 'unknown'}, {'voice_device': 'unknown'},
                     {'jc1060_ip': '999.1.2.3'}, {'jc1060_ip': 42}, {'unexpected': True}):
            with self.subTest(data=data), self.assertRaises(ValueError):
                self.boards.update(data)
        self.assertEqual(self.path.read_bytes(), before)

    def test_failed_atomic_write_keeps_previous_memory_and_disk(self):
        self.boards.update({'robot3axis_enabled': False})
        before = self.path.read_bytes()
        with patch('core.board_settings.os.replace', side_effect=OSError('locked')):
            with self.assertRaises(OSError): self.boards.update({'robot3axis_enabled': True})
        self.assertFalse(self.boards.snapshot()['robot3axis_enabled'])
        self.assertEqual(self.path.read_bytes(), before)

    def test_only_selected_terminal_can_update_live_connections(self):
        old, jc = '10.0.0.10', '10.0.0.42'
        tagged = {'source': 'HMI_MCU', 'board': 'JC1060P470C'}
        self.assertTrue(self.boards.accepts('hmi_mcu', {}, old, jc))
        self.assertFalse(self.boards.accepts('hmi_mcu', tagged, jc, jc))
        self.assertFalse(self.boards.accepts('drivemcu', {}, jc, jc))
        self.boards.update({'control_terminal': 'jc1060', 'voice_device': 'controlmcu'})
        self.assertFalse(self.boards.accepts('hmi_mcu', {}, old, jc))
        self.assertTrue(self.boards.accepts('hmi_mcu', tagged, jc, jc))
        self.assertTrue(self.boards.accepts('drivemcu', {}, old, jc))
        self.assertFalse(self.boards.accepts('drivemcu', {}, jc, jc))
        self.assertTrue(self.boards.accepts('drivemcu', {}, old))
        self.assertFalse(self.boards.accepts('drivemcu', {'board': 'JC1060P470C'}, jc))
        self.boards.update({'jc1060_ip': jc})
        self.assertFalse(self.boards.accepts('hmi_mcu', tagged, old, jc))

    def test_stats_hide_inactive_board_without_deleting_counters(self):
        stats = StatsManager()
        stats.record_mcu_packet('Robot3AxisMCU', 'heartbeat')
        stats.record_mcu_packet('HMI_MCU', 'heartbeat')
        stats.record_mcu_packet('DriveMCU', 'heartbeat')
        drive_before = deepcopy(stats.get_summary()['mcu_stats']['drivemcu'])
        self.boards.update({'robot3axis_enabled': False, 'control_terminal': 'jc1060'})
        filtered = self.boards.decorate_stats(stats.get_summary())
        self.assertNotIn('robot3axismcu', filtered['mcu_stats'])
        self.assertEqual(filtered['total_mcu_packets'], 2)
        self.assertEqual(filtered['mcu_stats']['drivemcu'], drive_before)
        self.assertEqual(filtered['mcu_stats']['hmi_mcu']['name'], 'JC1060 통합 단말')
        self.assertEqual(stats.get_summary()['total_mcu_packets'], 3)


class BoardRouteTests(unittest.TestCase):
    def setUp(self):
        BoardSettingsTests.setUp(self)
        self.app = Flask(__name__, template_folder=str(ROOT / 'frontend/templates'), static_folder=str(ROOT / 'frontend/static'))
        self.events = []
        self.env = {
            'board_settings': self.boards, 'DEVICE_NAMES': DEVICE_NAMES, 'device_key': device_key,
            'request': request, 'jsonify': jsonify, 'time': time, 'datetime': datetime,
            'stats_manager': StatsManager(), 'clients': [], 'threading': SimpleNamespace(Thread=lambda **kw: SimpleNamespace(start=lambda: None)),
            'tts_to_pcm_and_send': lambda *args: None, 'speak_guidance': lambda *args: None, 'get_local_ip': lambda: '127.0.0.1',
            'broadcast_event': self.events.append, 'STATUS_THRESHOLD': 10, 'EXCHANGE_RATE': 1500,
            'jc1060_seen_ip': '', 'jc1060_last_seen': 0.0, 'jc1060_voice_seen': 0.0,
            'jc1060_audio_ready': False, 'jc1060_mic_ready': False, 'jc1060_guidance_enabled': True, 'jc1060_voice_ingress': VoiceIngress(),
            'voice_selection_generation': 0, 'voice_job_context': threading.local(),
            'pending_hmi_actions': [], 'np': SimpleNamespace(array=lambda *args, **kw: [], int16=int),
        }
        for name in ('device_stats', 'last_seen', 'last_connection_state', 'device_korean_names', 'robot3axis_state', 'drive_settings', 'motor_state'):
            self.env[name] = literal(name)
        self.env['CONFIG'] = {'controlmcu': {'ip': '10.0.0.10', 'mode': 'VAD'}, 'hmi_mcu_ip': '10.0.0.11', 'drivemcu_ip': '10.0.0.12',
                              'imagesendermcu': {'camera_model': 'OV3660', 'resolution': 'SVGA', 'jpeg_quality': 12, 'transmission_mode': 'UDP'}, 'total_cost': 0}
        for name in ('get_jc1060_ip', 'get_voice_target_ip', 'get_device_status', 'get_status', 'on_boards_changed'):
            load_function(name, self.env)
        register_board_routes(self.app, self.boards, self.env['on_boards_changed'])
        for path, name in (('/api/heartbeat', 'receive_heartbeat'), ('/api/log', 'receive_log'),
                           ('/api/robot3axis/command', 'send_robot3axis_command'), ('/api/control/update', 'update_control_state')):
            self.app.add_url_rule(path, name, load_function(name, self.env), methods=['POST'])
        self.client = self.app.test_client()

    def test_settings_api_validation_failure_and_no_change(self):
        response = self.client.post('/api/hardware/config', json={'robot3axis_enabled': 'false'})
        self.assertEqual(response.status_code, 400)
        self.assertTrue(self.client.get('/api/hardware/config').json['hardware']['robot3axis_enabled'])
        self.assertEqual(self.events, [])

    def test_terminal_and_voice_switches_preserve_drive_state_and_settings(self):
        self.env['last_seen']['drivemcu'] = time.time()
        self.env['last_connection_state']['drivemcu'] = 'Online'
        self.env['device_stats']['DriveMCU'].update(ip='10.0.0.12', rssi=-55, tof_dist=143)
        drive_before = deepcopy(self.env['device_stats']['DriveMCU'])
        last_seen_before = self.env['last_seen']['drivemcu']
        motor_before = deepcopy(self.env['motor_state'])
        settings_before = deepcopy(self.env['drive_settings'])
        self.env['pending_hmi_actions'].append({'id': 'old', 'desc': 'old command'})
        response = self.client.post('/api/hardware/config', json={'control_terminal': 'jc1060', 'robot3axis_enabled': False})
        self.assertEqual(response.status_code, 200)
        for change in ({'control_terminal':'legacy'}, {'control_terminal':'jc1060', 'jc1060_ip':'10.0.0.42'}, {'voice_device':'controlmcu'}):
            self.assertEqual(self.client.post('/api/hardware/config',json=change).status_code,200)
            self.assertEqual(self.env['last_seen']['drivemcu'], last_seen_before)
            self.assertEqual(self.env['last_connection_state']['drivemcu'],'Online')
            self.assertEqual(self.env['device_stats']['DriveMCU'], drive_before)
            self.assertEqual(self.env['CONFIG']['drivemcu_ip'],'10.0.0.12')
            self.assertEqual(self.env['motor_state'], motor_before)
            self.assertEqual(self.env['drive_settings'], settings_before)
        self.assertEqual(self.env['pending_hmi_actions'], [])
        self.assertEqual(self.events[0]['type'], 'hardware_config')
        self.assertEqual(self.events[1]['status']['robot3axismcu']['state'], 'Disabled')
        self.assertEqual(BoardSettings(self.path).snapshot()['control_terminal'], 'jc1060')

    def test_disabled_robot_rejects_api_heartbeat_and_log(self):
        self.boards.update({'robot3axis_enabled': False})
        self.assertEqual(self.client.post('/api/robot3axis/command', json={'command': 'rx100'}).status_code, 409)
        for endpoint in ('heartbeat', 'log'):
            self.assertEqual(self.client.post('/api/' + endpoint, json={'source': 'Robot3AxisMCU'}).json['status'], 'ignored')
        self.assertEqual(self.env['last_seen']['robot3axismcu'], 0)

    def test_jc_heartbeat_detects_board_and_rejects_other_terminal(self):
        self.boards.update({'control_terminal': 'jc1060', 'voice_device': 'controlmcu'})
        def post(source, ip, **values):
            return self.client.post('/api/heartbeat', json={'source': source, **values}, environ_base={'REMOTE_ADDR': ip}).json
        self.assertEqual(post('HMI_MCU', '10.0.0.10')['status'], 'ignored')
        self.assertEqual(post('HMI_MCU', '10.0.0.42', board='JC1060P470C')['status'], 'ok')
        self.assertEqual(post('DriveMCU', '10.0.0.42')['status'], 'ignored')
        self.assertEqual(post('DriveMCU', '10.0.0.10')['status'], 'ok')
        self.assertEqual(self.env['device_stats']['DriveMCU']['ip'], '10.0.0.10')
        self.assertEqual(self.env['CONFIG']['hmi_mcu_ip'], '10.0.0.11')
        self.assertEqual(self.env['get_status']()['jc1060']['state'], 'Online')

    def test_jc_mode_ignores_old_joystick(self):
        self.boards.update({'control_terminal': 'jc1060', 'voice_device': 'controlmcu'})
        response = self.client.post('/api/control/update', json={'mode': 'A', 'joystick': {'up': True}})
        self.assertEqual(response.json['reason'], 'inactive_terminal')

    def test_voice_target_switches_independently(self):
        self.env['jc1060_seen_ip'] = '10.0.0.42'
        self.assertEqual(self.env['get_voice_target_ip'](), '10.0.0.10')
        self.boards.update({'voice_device': 'jc1060'})
        self.assertEqual(self.env['get_voice_target_ip'](), '10.0.0.42')

    def test_disabled_ai_tools_do_not_queue_robot_commands(self):
        self.boards.update({'robot3axis_enabled': False})
        for name, args in (('move_3axis_robot', (10, 20, 30)), ('control_pneumatic_gripper', ('open',))):
            result = load_function(name, self.env)(*args)
            self.assertIn('꺼져', result)

    def test_queued_robot_approval_cannot_run_after_disabling(self):
        callbacks = []
        self.env['queue_action'] = lambda desc, callback: callbacks.append(callback) or 'queued'
        move = load_function('move_3axis_robot', self.env)
        self.assertEqual(move(10, 20, 30), 'queued')
        before = deepcopy(self.env['robot3axis_state'])
        self.boards.update({'robot3axis_enabled': False})
        callbacks[0]()
        self.assertEqual(self.env['robot3axis_state'], before)
        self.assertEqual(self.events, [])

    def test_drive_commands_keep_the_existing_endpoint_under_both_terminals(self):
        sent = []
        def submit(ip, port, command, callback, allowed):
            if allowed():
                sent.extend(((ip, port), (command + '\n').encode()))
                callback(SimpleNamespace(ok=True, status='accepted', message='ACK:OK'))
        self.env['drive_commands'] = SimpleNamespace(submit=submit)
        class Socket:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def settimeout(self, value): pass
            def connect(self, address): sent.append(address)
            def sendall(self, data): sent.append(data)
        self.env['socket'] = SimpleNamespace(socket=lambda *args: Socket(), AF_INET=2, SOCK_STREAM=1)
        self.env['broadcast_system_log'] = lambda *args: None
        self.env['threading'] = SimpleNamespace(Thread=lambda target, **kw: SimpleNamespace(start=target))
        send = load_function('send_tcp_command_async', self.env)
        self.env['jc1060_seen_ip'] = '10.0.0.42'
        for terminal in ('legacy','jc1060'):
            self.boards.update({'control_terminal':terminal, 'robot3axis_enabled':False})
            sent.clear()
            send('10.0.0.30',5002,'rx100','Robot3Axis')
            send('10.0.0.42',5002,'mx100','DriveMCU')
            self.assertEqual(sent,[])
            send('10.0.0.12',5002,'mx100','DriveMCU')
            self.assertEqual(sent,[('10.0.0.12',5002),b'mx100\n'])

    def test_voice_switch_clears_recording_and_old_audio_queues(self):
        stopped = []
        self.env['voice_queue_manager'] = SimpleNamespace(stop=lambda: stopped.append('voice'))
        self.env['music_player'] = SimpleNamespace(stop=lambda: stopped.append('music'))
        self.env['is_recording'] = True
        self.env['audio_buffer'] = [1, 2, 3]
        response = self.client.post('/api/hardware/config', json={'voice_device': 'jc1060'})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(stopped, ['voice', 'music'])
        self.assertFalse(self.env['is_recording'])
        self.assertEqual(self.env['audio_buffer'], [])

    def test_templates_render_both_board_modes(self):
        # Empty test credentials and no AI/audio dependency imports.
        config = deepcopy(self.env['CONFIG'])
        config['api_key'] = ''
        for mode in ('legacy', 'jc1060'):
            self.boards.update({'control_terminal': mode})
            with self.app.test_request_context('/'):
                for template in ('index.html', 'settings.html'):
                    html = render_template(template, config=config, hardware=self.boards.snapshot(), server_ip='127.0.0.1')
                    self.assertIn('board-settings.js', html)
                    self.assertIn('"control_terminal": "' + mode + '"', html)
                    if template == 'settings.html': self.assertIn('board-config-form', html)


if __name__ == '__main__':
    unittest.main()
