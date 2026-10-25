"""Offline tests for the PC-driven camera preview. No network or camera."""
from pathlib import Path
import sys
import threading
import time
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend'))
from core.camera_stream import CameraStream
from core.sorting_pipeline import FrameInbox


class CameraStreamTests(unittest.TestCase):
    def setUp(self):
        self.inbox = FrameInbox()
        self.blocked = False
        self.triggers = 0
        self.lock = threading.Lock()

    def trigger(self):
        with self.lock:
            self.triggers += 1
        # The camera answers asynchronously, like the UDP receive path.
        threading.Timer(0.01, lambda: self.inbox.publish(b'frame')).start()

    def make(self, **kwargs):
        return CameraStream(self.trigger, self.inbox, lambda: self.blocked, gap=0.0, **kwargs)

    def test_captures_back_to_back_until_stopped(self):
        stream = self.make()
        self.assertTrue(stream.start())
        self.assertTrue(stream.active)
        self.assertFalse(stream.start())  # already running
        time.sleep(0.3)
        stream.stop()
        self.assertFalse(stream.active)
        count = self.triggers
        self.assertGreater(count, 3)
        time.sleep(0.1)
        self.assertLessEqual(self.triggers, count + 1)  # at most the capture in flight

    def test_sorting_blocks_and_ends_the_stream(self):
        self.blocked = True
        stream = self.make()
        self.assertFalse(stream.start())
        self.blocked = False
        self.assertTrue(stream.start())
        self.blocked = True
        time.sleep(0.2)
        self.assertFalse(stream.active)

    def test_time_limit_and_missing_camera(self):
        stream = CameraStream(lambda: None, self.inbox, lambda: False, max_seconds=0.2, gap=0.0, frame_timeout=0.05)
        self.assertTrue(stream.start())
        time.sleep(0.4)
        self.assertFalse(stream.active)


if __name__ == '__main__':
    unittest.main()
