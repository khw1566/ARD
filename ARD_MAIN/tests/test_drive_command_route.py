"""Exercise the active route without importing the audio/AI app or using MCUs."""
import ast
from pathlib import Path
import sys
import threading
from types import SimpleNamespace
import unittest
from flask import Flask, jsonify, request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
from network.drive_transport import CommandResult
from core.rail_control import disc_tilt_error


class DriveCommandRouteTests(unittest.TestCase):
    def setUp(self):
        self.sent, self.updates = [], []
        self.delivery = "accepted"
        self.env = {
            "request": request, "jsonify": jsonify,
            # 4:1 disc reducers as on the rig (.env): mxr200 tilts 45 deg, inside the ±60 deg limit.
            "CONFIG": {"drivemcu_ip": "10.0.0.22", "gear_ratio_x": 4.0, "gear_ratio_y": 4.0},
            "device_stats": {"DriveMCU": {"ip": "Unknown"}},
            "drive_settings": {}, "motor_state": {"mx_steps": 0, "my_steps": 0},
            "sorting_pipeline": SimpleNamespace(active=False, cancelled=threading.Event()),
            "board_settings": SimpleNamespace(accepts=lambda *args: True),
            "get_jc1060_ip": lambda: "", "update_motor_state": self.updates.append,
            "threading": SimpleNamespace(Thread=lambda **kwargs: SimpleNamespace(start=lambda: None)),
            "tts_to_pcm_and_send": lambda *args: None, "play_emergency_stop_voice": lambda: None,
            "stats_manager": SimpleNamespace(record_api_call=lambda *a, **kw: None),
            "rail_tools": SimpleNamespace(cancel=lambda: None),
            "disc_tilt_error": disc_tilt_error,
        }
        def send(ip, port, command, name, on_result):
            self.sent.append((ip, command))
            result = CommandResult(command, self.delivery, "fixture", "ACK:OK" if self.delivery == "accepted" else "ERR:REJECTED")
            on_result(result)
            result.done.set()
            return result
        self.env["send_tcp_command_async"] = send
        source = ROOT / "backend/app.py"
        node = next(n for n in ast.parse(source.read_text(encoding="utf-8-sig")).body
                    if isinstance(n, ast.FunctionDef) and n.name == "send_drive_command")
        node.decorator_list = []
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), "exec"), self.env)
        app = Flask(__name__)
        app.add_url_rule("/command", view_func=self.env["send_drive_command"], methods=["POST"])
        self.client = app.test_client()

    def test_accepted_command_updates_display_once(self):
        response = self.client.post("/command", json={"command": "mxr200"})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["ack"], "ACK:OK")
        self.assertEqual(self.updates, ["mxr200"])

    def test_rejected_command_does_not_update_settings_or_motion(self):
        self.delivery = "rejected"
        response = self.client.post("/command", json={"command": "mzm1"})
        self.assertEqual(response.status_code, 409)
        self.assertEqual(self.env["drive_settings"], {})
        self.assertEqual(self.updates, [])

    def test_lost_ack_is_error_without_state_update(self):
        self.delivery = "unconfirmed"
        response = self.client.post("/command", json={"command": "mxr200"})
        self.assertEqual(response.status_code, 504)
        self.assertEqual(self.updates, [])

    def test_no_ip_is_error_not_fake_success(self):
        self.env["CONFIG"]["drivemcu_ip"] = ""
        self.assertEqual(self.client.post("/command", json={"command": "mxr1"}).status_code, 503)
        self.assertEqual(self.sent, [])
        self.assertEqual(self.updates, [])

    def test_disc_tilt_past_60_is_refused_before_sending(self):
        response = self.client.post("/command", json={"command": "df134"})  # 60.3 deg
        self.assertEqual(response.status_code, 409)
        self.assertIn("60", response.json["message"])
        self.assertEqual(self.sent, [])
        self.assertEqual(self.client.post("/command", json={"command": "dr2000"}).status_code, 200)  # rotation

    def test_heartbeat_ip_can_be_used(self):
        self.env["CONFIG"]["drivemcu_ip"] = ""
        self.env["device_stats"]["DriveMCU"]["ip"] = "10.0.0.22"
        self.assertEqual(self.client.post("/command", json={"command": "mxr1"}).status_code, 200)
        self.assertEqual(self.sent, [("10.0.0.22", "mxr1")])

    def test_jc1060_address_is_rejected(self):
        self.env["board_settings"].accepts = lambda *a: False
        self.assertEqual(self.client.post("/command", json={"command": "mxr1"}).status_code, 409)
        self.assertEqual(self.sent, [])

    def test_busy_sorter_does_not_change_settings(self):
        self.env["sorting_pipeline"].active = True
        self.assertEqual(self.client.post("/command", json={"command": "mzh200"}).status_code, 409)
        self.assertEqual(self.env["drive_settings"], {})
        self.assertEqual(self.sent, [])

    def test_stop_cancels_sorter_and_is_still_sent(self):
        self.env["sorting_pipeline"].active = True
        self.assertEqual(self.client.post("/command", json={"command": "x"}).status_code, 200)
        self.assertTrue(self.env["sorting_pipeline"].cancelled.is_set())
        self.assertEqual(self.sent, [("10.0.0.22", "x")])

    def test_invalid_payload(self):
        for payload in ({}, {"command": 42}, [], {"command": " "}):
            self.assertEqual(self.client.post("/command", json=payload).status_code, 400)
        self.assertEqual(self.sent, [])


if __name__ == "__main__":
    unittest.main()
