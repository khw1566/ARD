"""Automatic PC announcements: no repeats while a control is held, and the JC1060 switch."""
from pathlib import Path
import sys
import threading
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_board_settings import load_function


class GuidanceTests(unittest.TestCase):
    def setUp(self):
        self.now, self.spoken, self.device = 100.0, [], "jc1060"
        fake_time = SimpleNamespace(monotonic=lambda: self.now)
        run_now = SimpleNamespace(Thread=lambda target, args, daemon: SimpleNamespace(start=lambda: target(*args)))
        self.env = {"time": fake_time, "threading": run_now, "_guidance_lock": threading.Lock(), "_guidance_last": {},
                    "GUIDANCE_REPEAT_S": 8.0, "jc1060_guidance_enabled": True,
                    "board_settings": SimpleNamespace(snapshot=lambda: {"voice_device": self.device}),
                    "tts_to_pcm_and_send": self.spoken.append}
        for name in ("guidance_muted", "guidance_due", "speak_guidance"):
            load_function(name, self.env)
        self.speak = self.env["speak_guidance"]

    def test_repeated_drive_commands_announce_once_until_idle(self):
        for _ in range(20):  # e.g. a held +1 button or rail button
            self.speak("분류구조 작동합니다.")
            self.now += 0.5
        self.assertEqual(self.spoken, ["분류구조 작동합니다."])
        self.now += 8.1
        self.speak("분류구조 작동합니다.")
        self.assertEqual(len(self.spoken), 2)

    def test_different_sentences_are_independent(self):
        self.speak("A")
        self.speak("B")
        self.assertEqual(self.spoken, ["A", "B"])

    def test_jc1060_switch_mutes_only_when_it_is_the_voice_board(self):
        self.env["jc1060_guidance_enabled"] = False
        self.assertFalse(self.speak("A"))
        self.device = "controlmcu"
        self.assertTrue(self.speak("A"))
        self.assertEqual(self.spoken, ["A"])

    def test_estop_window_is_fixed_not_sliding(self):
        due = self.env["guidance_due"]
        self.assertTrue(due("__estop__", 2.0, sliding=False))
        self.now += 1.5
        self.assertFalse(due("__estop__", 2.0, sliding=False))
        self.now += 0.6
        self.assertTrue(due("__estop__", 2.0, sliding=False))


if __name__ == "__main__":
    unittest.main()
