"""Network/HMI input policy against a fake DriveMCU; no physical motion."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from flask import Flask

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from api.jc1060_drive_routes import register_jc1060_drive_routes


class RemoteDriveTests(unittest.TestCase):
    def setUp(self):
        self.jobs, self.estimates = [], []
        self.mode = "jc1060"
        self.sorter = SimpleNamespace(active=False)
        self.sample = {"protocol": 1, "boot_id": 42, "busy": False, "pid": False}
        self.boards = SimpleNamespace(snapshot=lambda: {"control_terminal": self.mode}, accepts=lambda *a: True)
        self.app = Flask(__name__)
        dispatcher = SimpleNamespace(submit=lambda ip, port, command, **kw: self.jobs.append((ip, command, kw)))
        self.dispatcher = dispatcher
        self.state = register_jc1060_drive_routes(self.app, self.boards, lambda: "10.0.0.42", lambda: "10.0.0.12",
            dispatcher, lambda: self.sorter, self.estimates.append, lambda ip: self.sample.copy())
        self.now = 1.0
        self.state.clock = lambda: self.now
        self.client = self.app.test_client()
        self.app.add_url_rule("/api/drivemcu/command", "manual", lambda: ("ok", 200), methods=["POST"])

    def post(self, data, peer="10.0.0.42"):
        return self.client.post("/api/jc1060/drive", json=data, environ_base={"REMOTE_ADDR": peer})

    def arm(self):
        self.assertEqual(self.post({"op": "arm"}).json["status"], "armed")

    def complete(self, job, ok=True):
        job[2]["callback"](SimpleNamespace(ok=ok, command=job[1]))

    def test_requires_selected_hmi_source_and_explicit_arm(self):
        self.assertEqual(self.post({"x": 1}, peer="10.0.0.9").status_code, 403)
        self.assertEqual(self.post({"x": 1}).json["status"], "disarmed")
        self.mode = "legacy"
        self.assertEqual(self.post({"op": "arm"}).status_code, 403)
        self.assertEqual(self.jobs, [])

    def test_finite_axes_and_single_pending_batch(self):
        self.arm()
        self.assertEqual(self.post({"x": 1, "y": -1, "z": 0.5}).status_code, 202)
        self.assertEqual([(j[0], j[1]) for j in self.jobs], [("10.0.0.12", "b20,-20"), ("10.0.0.12", "mzr10")])
        self.assertEqual(self.post({"x": -1}).json["status"], "busy")
        self.assertEqual(len(self.jobs), 2)
        for job in self.jobs:
            self.assertTrue(job[2]["allowed"]())
            self.complete(job)
        self.assertEqual(self.estimates, ["b20,-20", "mzr10"])
        self.assertEqual(self.state.pending, 0)

    def test_unsent_pulse_expires_and_neutral_invalidates(self):
        self.arm()
        self.post({"x": 1})
        job = self.jobs[0]
        self.now += 0.301
        self.assertFalse(job[2]["allowed"]())
        self.post({"x": 0})
        self.assertEqual(self.jobs[-1][1], "x")
        self.assertFalse(job[2]["allowed"]())

    def test_manual_pc_override_disarms_and_cancels_pending_jog(self):
        self.arm()
        self.post({"x": 1})
        self.client.post("/api/drivemcu/command", json={"command": "mxr50"})
        self.assertFalse(self.jobs[0][2]["allowed"]())
        self.assertEqual(self.jobs[-1][1], "x")
        self.assertFalse(self.state.armed)

    def test_does_not_queue_more_motion_while_physical_board_busy(self):
        self.arm()
        self.sample["busy"] = True
        self.assertEqual(self.post({"x": 1}).json["status"], "busy")
        self.assertEqual(self.jobs, [])

    def test_board_reboot_and_pid_require_rearm(self):
        self.arm()
        self.sample["boot_id"] += 1
        self.assertEqual(self.post({"x": 1}).json["status"], "disarmed")
        self.sample["pid"] = True
        self.assertEqual(self.post({"op": "arm"}).status_code, 409)
        self.assertEqual(self.jobs, [])

    def test_sorting_and_invalid_values_cannot_move(self):
        self.arm()
        for value in (True, "1", 1.1, float("nan")):
            self.assertEqual(self.post({"x": value}).status_code, 400)
        self.sorter.active = True
        self.assertEqual(self.post({"x": 1}).json["status"], "disarmed")
        self.assertEqual(self.jobs, [])

    def test_failed_ack_disarms_without_advancing_estimate(self):
        self.arm()
        self.post({"x": 1})
        self.complete(self.jobs[0], ok=False)
        self.assertFalse(self.state.armed)
        self.assertEqual(self.estimates, [])

    def test_explicit_stop_reaches_board_without_active_analog_input(self):
        self.assertEqual(self.post({"op": "stop"}).status_code, 200)
        self.assertEqual([(j[0], j[1]) for j in self.jobs], [("10.0.0.12", "x")])

    def test_incomplete_status_response_fails_closed(self):
        class Incomplete(dict):
            def copy(self):
                raise RuntimeError("DriveMCU response incomplete")
        self.sample = Incomplete()
        self.assertEqual(self.post({"op": "arm"}).status_code, 503)
        self.assertFalse(self.state.armed)
        self.assertEqual(self.jobs, [])

    def test_voice_and_ai_sender_also_take_over_from_gamepad(self):
        from test_board_settings import load_function
        self.arm()
        self.post({"x": 1})
        env = {"device_key": lambda name: "drivemcu", "drive_commands": self.dispatcher,
               "jc1060_drive_input": self.state, "board_settings": self.boards,
               "get_jc1060_ip": lambda: "10.0.0.42"}
        sender = load_function("send_tcp_command_async", env)
        sender("10.0.0.12", 5002, "mxr50", "DriveMCU")
        self.assertFalse(self.state.armed)
        self.assertFalse(self.jobs[0][2]["allowed"]())
        self.assertEqual([j[1] for j in self.jobs[-2:]], ["x", "mxr50"])


if __name__ == "__main__":
    unittest.main()
