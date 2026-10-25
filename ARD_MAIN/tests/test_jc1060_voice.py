from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

from flask import Flask

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'backend'))
from core.board_settings import BoardSettings
from api.jc1060_voice_routes import (VoiceIngress, GuidanceQueue, register_jc1060_voice_routes,
                                     register_jc1060_guidance_route, MAX_PCM_BYTES, PCM_FORMAT)
from test_board_settings import load_function


class DeferredThreads:
    def __init__(self): self.pending = []
    def __call__(self, target, **kwargs):
        return SimpleNamespace(start=lambda: self.pending.append(target))


class OnboardVoiceTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT.parent / 'tmp'
        scratch.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.settings = BoardSettings(Path(temporary.name) / 'boards.json')
        self.settings.update({'control_terminal': 'jc1060', 'jc1060_ip': '10.0.0.42'})
        self.threads = DeferredThreads()
        self.ingress = VoiceIngress(self.threads)
        self.received, self.errors = [], []
        self.app = Flask(__name__)
        register_jc1060_voice_routes(self.app, self.settings, lambda: self.settings.snapshot()['jc1060_ip'],
                                    self.ingress, self.received.append, lambda: None, self.errors.append)
        self.client = self.app.test_client()
        self.headers = {'Content-Type': 'application/octet-stream', 'X-Audio-Format': PCM_FORMAT}
        self.pcm = b'\x00\x80\xff\x7f' * 800

    def post(self, data=None, ip='10.0.0.42', headers=None):
        return self.client.post('/api/jc1060/voice', data=self.pcm if data is None else data,
                                headers=self.headers if headers is None else headers,
                                environ_base={'REMOTE_ADDR': ip})

    def test_terminal_switch_selects_onboard_input_but_explicit_voice_override_is_kept(self):
        self.assertEqual(self.settings.snapshot()['voice_device'], 'jc1060')
        self.settings.update({'voice_device': 'controlmcu'})
        self.assertEqual(self.post().status_code, 409)
        self.settings.update({'control_terminal': 'legacy'})
        self.settings.update({'control_terminal': 'jc1060', 'voice_device': 'controlmcu'})
        self.assertEqual(self.settings.snapshot()['voice_device'], 'controlmcu')

    def test_accepts_exact_pcm_once_and_publishes_completion(self):
        response = self.post()
        self.assertEqual(response.status_code, 202)
        self.assertEqual(response.json['job_id'], 1)
        self.assertTrue(self.ingress.snapshot()['busy'])
        self.assertEqual(self.received, [])
        self.threads.pending.pop()()
        self.assertEqual(self.received, [self.pcm])
        self.assertEqual(self.ingress.snapshot()['completed_job_id'], 1)
        self.assertFalse(self.ingress.snapshot()['busy'])
        self.assertGreater(self.ingress.snapshot()['session'], 0)

    def test_limits_concurrent_voice_jobs(self):
        self.assertEqual(self.post().status_code, 202)
        self.assertEqual(self.post().status_code, 429)
        self.assertEqual(len(self.threads.pending), 1)
        self.threads.pending.pop()()
        self.assertEqual(self.post().json['job_id'], 2)

    def test_only_selected_board_and_pcm_format_are_accepted(self):
        self.assertEqual(self.post(ip='10.0.0.10').status_code, 403)
        self.assertEqual(self.post(headers={'Content-Type': 'audio/wav'}).status_code, 415)
        self.settings.update({'voice_device': 'controlmcu'})
        self.assertEqual(self.post().status_code, 409)
        self.assertEqual(len(self.threads.pending), 0)

    def test_length_and_alignment_limits(self):
        for data in (b'', b'\x00' * 3198, b'\x00' * 3201):
            with self.subTest(length=len(data)):
                self.assertEqual(self.post(data).status_code, 400)
        self.assertEqual(self.post(b'\x00' * (MAX_PCM_BYTES + 2)).status_code, 413)
        self.assertFalse(self.ingress.snapshot()['busy'])

    def test_failed_processing_releases_gate_and_records_completion(self):
        def failed(_): raise RuntimeError('fixture failure')
        self.ingress.submit(self.pcm, failed, lambda: None, self.errors.append)
        self.threads.pending.pop()()
        self.assertFalse(self.ingress.snapshot()['busy'])
        self.assertEqual(self.ingress.snapshot()['completed_job_id'], 1)
        self.assertEqual(str(self.errors[0]), 'fixture failure')

    def test_thread_creation_failure_does_not_stick_busy(self):
        def failed(**kwargs): raise RuntimeError('no worker')
        with self.assertRaises(RuntimeError):
            self.ingress.submit(self.pcm, self.received.append, lambda: None, self.errors.append, failed)
        self.assertFalse(self.ingress.snapshot()['busy'])

    def test_accepted_job_keeps_its_original_selection_context(self):
        context = [5]
        seen = []
        app = Flask('context_fixture')
        register_jc1060_voice_routes(app, self.settings, lambda: '10.0.0.42', self.ingress,
            lambda raw, generation: seen.append(('pcm', generation)),
            lambda generation: seen.append(('start', generation)), self.errors.append, lambda: context[0])
        response = app.test_client().post('/api/jc1060/voice', data=self.pcm, headers=self.headers,
                                         environ_base={'REMOTE_ADDR': '10.0.0.42'})
        self.assertEqual(response.status_code, 202)
        context[0] = 6
        self.threads.pending.pop()()
        self.assertEqual(seen, [('start', 5), ('pcm', 5)])

    def test_old_voice_job_cannot_run_after_board_selection_changes(self):
        import threading
        processed = []
        env = {'voice_selection_generation': 6, 'voice_job_context': threading.local(),
               'board_settings': self.settings, 'process_voice_command': processed.append}
        process = load_function('process_jc1060_voice', env)
        process(self.pcm, 5)
        self.assertEqual(processed, [])
        self.assertFalse(hasattr(env['voice_job_context'], 'generation'))

    def test_guidance_coalesces_expires_and_prioritizes_emergency(self):
        clock = [0.0]
        spoken = []
        queue = GuidanceQueue(lambda key, text, context: spoken.append((key, text)), lambda context: context == 'current',
                               clock=lambda: clock[0], thread_factory=DeferredThreads())
        self.assertTrue(queue.submit('drive_move', 'current'))
        self.assertFalse(queue.submit('drive_move', 'current'))
        queue.submit('home', 'current')
        queue.submit('emergency', 'current')
        queue.run_one()
        self.assertEqual(spoken, [('emergency', '긴급 정지!')])
        queue.run_one()
        self.assertEqual(len(spoken), 1)
        clock[0] = 1
        queue.submit('home', 'current')
        clock[0] = 7
        queue.run_one()
        self.assertEqual(len(spoken), 1)
        queue.submit('mode_robot', 'old')
        queue.run_one()
        self.assertEqual(len(spoken), 1)

    def test_guidance_route_uses_allowlist_and_selected_terminal(self):
        requests = []
        queue = SimpleNamespace(submit=lambda *args: requests.append(args) or True)
        register_jc1060_guidance_route(self.app, self.settings, lambda: '10.0.0.42', queue, lambda: 5)
        def post(cue, ip='10.0.0.42'):
            return self.client.post('/api/jc1060/guidance', json={'cue': cue}, environ_base={'REMOTE_ADDR': ip})
        self.assertEqual(post('emergency').status_code, 202)
        self.assertEqual(requests, [('emergency', 5)])
        self.assertEqual(post('arbitrary text').status_code, 400)
        self.assertEqual(post('home', '10.0.0.10').status_code, 409)

    def test_onboard_volume_is_applied_once_and_legacy_gain_is_retained(self):
        env = {'board_settings': self.settings, 'CONFIG': {'controlmcu': {'volume': 7}}}
        gain = load_function('get_voice_output_gain', env)
        self.assertEqual(gain(), 1.0)
        self.settings.update({'voice_device': 'controlmcu'})
        self.assertAlmostEqual(gain(), 7 / 21)

    def test_audio_stop_is_namespaced_and_motor_stop_is_preserved(self):
        from core.board_settings import device_key
        from core.stats_manager import StatsManager
        transmitted = []
        class Socket:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def settimeout(self, value): pass
            def connect(self, address): pass
            def sendall(self, data): transmitted.append(data)
        env = {'board_settings': self.settings, 'device_key': device_key,
               'get_jc1060_ip': lambda: '10.0.0.42', 'get_voice_target_ip': lambda: '10.0.0.42',
               'socket': SimpleNamespace(socket=lambda *args: Socket(), AF_INET=2, SOCK_STREAM=1),
               'stats_manager': StatsManager(), 'broadcast_system_log': lambda *args: None,
               'threading': SimpleNamespace(Thread=lambda target, **kw: SimpleNamespace(start=target))}
        def submit(ip, port, command, callback, allowed):
            self.assertTrue(allowed())
            transmitted.append((command + '\n').encode())
            callback(SimpleNamespace(ok=True, status='accepted', message='ACK:OK'))
        env['drive_commands'] = SimpleNamespace(submit=submit)
        send = load_function('send_tcp_command_async', env)
        send('10.0.0.42', 5002, 'stop', 'ControlMCU')
        send('10.0.0.12', 5002, 'x', 'DriveMCU')
        self.assertEqual(transmitted, [b'audio_stop\n', b'x\n'])


if __name__ == '__main__': unittest.main()
