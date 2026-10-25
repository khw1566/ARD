"""Live camera preview: request the next capture as soon as the previous frame arrives.

The ImageSender firmware only knows single captures, so streaming is driven from the
PC. Frames go through the normal receive path (web dashboard, HMI images) but are not
sent to the AI while streaming. Sorting has priority: the stream will not start, and
stops by itself, while `blocked()` is true.
"""
import threading
import time


class CameraStream:
    def __init__(self, trigger, inbox, blocked, max_seconds=300, gap=0.15, frame_timeout=3.0):
        self.trigger, self.inbox, self.blocked = trigger, inbox, blocked
        self.max_seconds, self.gap, self.frame_timeout = max_seconds, gap, frame_timeout
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._stop.set()
        self._thread = None

    @property
    def active(self):
        return not self._stop.is_set()

    def start(self):
        with self._lock:
            if self.active or self.blocked():
                return False
            self._stop = threading.Event()
            self._thread = threading.Thread(target=self._run, args=(self._stop,), daemon=True)
            self._thread.start()
            return True

    def stop(self):
        with self._lock:
            self._stop.set()

    def _run(self, stop):
        # Bounded so a forgotten preview does not keep the camera and Wi-Fi busy.
        deadline = time.monotonic() + self.max_seconds
        try:
            while not stop.is_set() and time.monotonic() < deadline and not self.blocked():
                with self.inbox.condition:
                    revision = self.inbox.revision
                try:
                    self.trigger()
                except Exception:
                    pass  # an offline camera is retried after the frame timeout
                with self.inbox.condition:
                    self.inbox.condition.wait_for(lambda: self.inbox.revision != revision or stop.is_set(),
                                                  timeout=self.frame_timeout)
                stop.wait(self.gap)
        finally:
            stop.set()
