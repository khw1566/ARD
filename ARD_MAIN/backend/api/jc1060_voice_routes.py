"""Bounded utterances from the JC1060 microphone, using the existing PC STT/AI/TTS path."""
import secrets
import threading
import time
from collections import deque

from flask import jsonify, request

MAX_PCM_BYTES = 16000 * 2 * 20
MIN_PCM_BYTES = 16000 * 2 // 10
PCM_FORMAT = 'pcm_s16le;rate=16000;channels=1'
GUIDANCE = {'drive_move': '분류구조 작동합니다.', 'home': '원점으로 이동합니다.',
            'mode_drive': '구동 모드입니다.', 'mode_robot': '3축 로봇 모드입니다.',
            'emergency': '긴급 정지!'}


class GuidanceQueue:
    """One worker, eight short-lived/coalesced announcements; emergency takes priority."""
    def __init__(self, speak, still_active, clock=time.monotonic, thread_factory=threading.Thread):
        self._speak, self._active, self._clock = speak, still_active, clock
        self._thread_factory = thread_factory
        self._condition = threading.Condition()
        self._queue = deque()
        self._last = {}
        self._thread = None

    def submit(self, cue, context):
        if cue not in GUIDANCE:
            raise ValueError('Unknown guidance cue')
        with self._condition:
            now = self._clock()
            if now - self._last.get(cue, -100) < 0.7:
                return False
            if cue == 'emergency':
                self._queue.clear()
            if len(self._queue) >= 8:
                return False
            self._last[cue] = now
            self._queue.append((cue, context, now))
            if self._thread is None:
                self._thread = self._thread_factory(target=self._worker, daemon=True, name='jc1060_guidance')
                try:
                    self._thread.start()
                except Exception:
                    self._thread = None
                    self._queue.clear()
                    raise
            self._condition.notify()
            return True

    def run_one(self):
        with self._condition:
            if not self._queue:
                return
            cue, context, stamp = self._queue.popleft()
        if self._clock() - stamp <= 5 and self._active(context):
            self._speak(cue, GUIDANCE[cue], context)

    def _worker(self):
        while True:
            with self._condition:
                self._condition.wait_for(lambda: bool(self._queue))
            try:
                self.run_one()
            except Exception:
                # TTS has its own logging; one failed announcement must not kill the worker.
                continue


class VoiceIngress:
    def __init__(self, thread_factory=threading.Thread):
        self._lock = threading.Lock()
        self._busy = False
        self._job = 0
        self._completed = 0
        self._session = secrets.randbelow(0xFFFFFFFE) + 1
        self._thread_factory = thread_factory

    def snapshot(self):
        with self._lock:
            return {'busy': self._busy, 'job_id': self._job,
                    'completed_job_id': self._completed, 'session': self._session}

    def submit(self, pcm, process, on_start, on_error, thread_factory=None):
        with self._lock:
            if self._busy:
                return None
            self._job += 1
            job = self._job
            self._busy = True

        def run():
            try:
                on_start()
                process(pcm)
            except Exception as exc:
                on_error(exc)
            finally:
                with self._lock:
                    self._completed = job
                    self._busy = False

        try:
            (thread_factory or self._thread_factory)(target=run, daemon=True, name='jc1060_voice').start()
        except Exception:
            with self._lock:
                self._completed = job
                self._busy = False
            raise
        return job


def register_jc1060_voice_routes(app, settings, get_board_ip, ingress, process, on_start, on_error, get_context=None):
    @app.route('/api/jc1060/voice', methods=['POST'])
    def jc1060_voice():
        if settings.snapshot()['voice_device'] != 'jc1060':
            return jsonify(status='error', message='JC1060 음성 보드를 선택하세요.'), 409
        expected = get_board_ip()
        if not expected or request.remote_addr != expected:
            return jsonify(status='error', message='선택한 JC1060 보드의 입력만 허용합니다.'), 403
        context = get_context() if get_context else None
        if request.mimetype != 'application/octet-stream' or request.headers.get('X-Audio-Format') != PCM_FORMAT:
            return jsonify(status='error', message='16 kHz mono PCM16LE 형식이 필요합니다.'), 415
        if request.content_length is not None and request.content_length > MAX_PCM_BYTES:
            return jsonify(status='error', message='녹음은 최대 20초입니다.'), 413
        if ingress.snapshot()['busy']:
            return jsonify(status='error', message='이전 음성을 처리 중입니다.'), 429
        # Read a bounded body even when the client does not send Content-Length.
        pcm = request.stream.read(MAX_PCM_BYTES + 1)
        if len(pcm) > MAX_PCM_BYTES:
            return jsonify(status='error', message='녹음은 최대 20초입니다.'), 413
        if len(pcm) < MIN_PCM_BYTES or len(pcm) % 2:
            return jsonify(status='error', message='PCM 길이가 올바르지 않습니다.'), 400
        # Re-check selection after receiving the body: a desktop switch may happen during upload.
        if settings.snapshot()['voice_device'] != 'jc1060' or get_board_ip() != expected or (get_context and get_context() != context):
            return jsonify(status='error', message='음성 보드 구성이 변경되었습니다.'), 409
        process_job = (lambda raw: process(raw, context)) if get_context else process
        begin_job = (lambda: on_start(context)) if get_context else on_start
        try:
            job = ingress.submit(pcm, process_job, begin_job, on_error)
        except Exception:
            return jsonify(status='error', message='음성 작업자를 시작하지 못했습니다.'), 503
        if job is None:
            return jsonify(status='error', message='이전 음성을 처리 중입니다.'), 429
        return jsonify(status='accepted', job_id=job), 202


def register_jc1060_guidance_route(app, settings, get_board_ip, queue, get_context):
    @app.route('/api/jc1060/guidance', methods=['POST'])
    def jc1060_guidance():
        if settings.snapshot()['control_terminal'] != 'jc1060' or request.remote_addr != get_board_ip():
            return jsonify(status='ignored'), 409
        data = request.get_json(silent=True)
        cue = data.get('cue') if isinstance(data, dict) else None
        if not isinstance(cue, str) or cue not in GUIDANCE:
            return jsonify(status='error', message='지원하지 않는 안내입니다.'), 400
        try:
            queued = queue.submit(cue, get_context())
        except Exception:
            return jsonify(status='error', message='안내 작업자를 시작하지 못했습니다.'), 503
        return jsonify(status='queued' if queued else 'coalesced'), 202
