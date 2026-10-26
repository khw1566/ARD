"""PC time fallback endpoint contract; no firmware build or hardware access."""
import time
import unittest
from types import SimpleNamespace
from flask import Flask, jsonify
from test_board_settings import load_function


class HmiClockTests(unittest.TestCase):
    def test_updates_expose_current_utc_epoch_and_keep_existing_fields(self):
        app = Flask(__name__)
        endpoint = load_function("get_hmi_updates", {
            "jsonify": jsonify, "sorting_status": {"is_running": False},
            "hmi_image_seq": 7, "camera_stream": SimpleNamespace(active=True),
        })
        with app.app_context():
            before = time.time()
            data = endpoint().get_json()
            after = time.time()
        self.assertLessEqual(before, data["unix_time"])
        self.assertLessEqual(data["unix_time"], after)
        self.assertEqual(data["image_seq"], 7)
        self.assertTrue(data["stream"])
        self.assertEqual(data["sorting"], {"is_running": False})
