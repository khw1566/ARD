"""Network/HMI input policy against a fake DriveMCU; no physical motion."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from flask import Flask

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from api.jc1060_drive_routes import register_jc1060_drive_routes, joystick_rates


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

    def test_pulse_speed_changes_interval_not_step_size(self):
        self.arm()
        self.assertEqual(self.post({"op": "pulse", "x": 0.5, "interval_ms": 200}).json["status"], "queued")
        self.assertEqual(self.jobs[0][1], "b20,0")
        self.complete(self.jobs[0])
        self.now += 0.399
        self.assertEqual(self.post({"op": "pulse", "x": 0.5, "interval_ms": 200}).json["status"], "waiting")
        self.now += 0.002
        self.assertEqual(self.post({"op": "pulse", "x": 0.5, "interval_ms": 200}).json["status"], "queued")
        self.assertEqual(self.jobs[-1][1], "b20,0")

    def test_pulse_cadence_begins_after_ack_and_reversal_cannot_bypass_it(self):
        self.arm()
        self.post({"op": "pulse", "x": 1, "interval_ms": 300})
        self.now += 0.2
        self.complete(self.jobs[0])
        self.now += 0.299
        self.assertEqual(self.post({"op": "pulse", "x": -1, "interval_ms": 300}).json["status"], "waiting")
        self.now += 0.002
        self.assertEqual(self.post({"op": "pulse", "x": -1, "interval_ms": 300}).json["status"], "queued")
        self.assertEqual(self.jobs[-1][1], "b-20,0")

    def test_pulse_axes_have_independent_cadence_and_no_pending_fifo(self):
        self.arm()
        self.post({"op": "pulse", "x": 1, "y": -0.5, "interval_ms": 200})
        self.assertEqual(self.jobs[0][1], "b20,-20")
        self.assertEqual(self.post({"op": "pulse", "x": 1}).json["status"], "busy")
        self.complete(self.jobs[0])
        self.now += 0.201
        self.post({"op": "pulse", "x": 1, "y": -0.5, "interval_ms": 200})
        self.assertEqual(self.jobs[-1][1], "b20,0")

    def test_pulse_neutral_expires_unsent_moves_and_allows_new_hold(self):
        self.arm()
        self.post({"op": "pulse", "z": 0.2, "interval_ms": 640})
        first = self.jobs[0]
        self.assertEqual(first[1], "mzr20")
        self.assertEqual(self.post({"op": "pulse", "x": 0}).json["status"], "neutral")
        self.assertFalse(first[2]["allowed"]())
        self.assertEqual(self.jobs[-1][1], "x")
        self.complete(first, ok=False)
        self.assertTrue(self.state.armed)
        self.assertEqual(self.post({"op": "pulse", "z": -1}).json["status"], "queued")

    def test_pulse_interval_and_deadzone_are_validated(self):
        self.arm()
        for value in (True, "200", 100.1, 99, 1001, float("nan")):
            self.assertEqual(self.post({"op": "pulse", "x": 1, "interval_ms": value}).status_code, 400)
        self.assertEqual(self.post({"op": "pulse", "x": 0.1}).json["status"], "neutral")
        self.assertEqual(self.jobs, [])

    def test_pulse_obeys_reboot_pid_busy_and_sorting_interlocks(self):
        self.arm()
        self.sample["busy"] = True
        self.assertEqual(self.post({"op": "pulse", "x": 1}).json["status"], "busy")
        self.sample["busy"] = False
        self.sample["boot_id"] += 1
        self.assertEqual(self.post({"op": "pulse", "x": 1}).json["status"], "disarmed")
        self.assertEqual(self.jobs, [])

    def test_pulse_expires_without_replaying_after_network_delay(self):
        self.arm()
        self.post({"op": "pulse", "x": 1})
        self.now += 0.301
        self.assertFalse(self.jobs[0][2]["allowed"]())

    def test_manual_mode_disarm_does_not_cancel_sorting(self):
        self.arm()
        self.post({"op": "pulse", "x": 1})
        first = self.jobs[0]
        self.sorter.active = True
        self.assertEqual(self.post({"op": "disarm"}).json["status"], "disarmed")
        self.assertFalse(self.state.armed)
        self.assertFalse(first[2]["allowed"]())
        self.assertTrue(self.sorter.active)
        self.assertEqual(self.jobs[-1][1], "x")

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

    def test_joystick_radial_deadzone_has_no_second_per_axis_deadzone(self):
        self.arm()
        self.assertEqual(self.post({"op": "joystick", "x": 0.14, "y": 0.14}).json["status"], "neutral")
        self.assertEqual(self.jobs, [])
        self.assertEqual(self.post({"op": "joystick", "x": 0.15, "y": 0.15}).json["status"], "queued")
        self.assertEqual(self.jobs[0][1], "b20,0")

    def test_joystick_quadratic_rate_paces_fixed_steps_from_ack(self):
        self.arm()
        self.post({"op": "joystick", "y": 0.6, "interval_ms": 200})
        self.assertEqual(self.jobs[0][1], "b20,-20")
        self.now += 0.1
        self.complete(self.jobs[0])
        self.now += 0.799
        self.assertEqual(self.post({"op": "joystick", "y": 0.6, "interval_ms": 200}).json["status"], "waiting")
        self.now += 0.002
        self.assertEqual(self.post({"op": "joystick", "y": 0.6, "interval_ms": 200}).json["status"], "queued")
        self.assertEqual(self.jobs[-1][1], "b20,-20")

    def test_joystick_acceleration_does_not_keep_previous_slow_delay(self):
        self.arm()
        self.post({"op": "joystick", "y": 0.4, "interval_ms": 200})
        self.complete(self.jobs[0])
        self.now += 0.201
        self.assertEqual(self.post({"op": "joystick", "y": 1, "interval_ms": 200}).json["status"], "queued")
        self.assertEqual(len(self.jobs), 2)

    def test_joystick_deceleration_and_reversal_obey_current_rate(self):
        self.arm()
        self.post({"op": "joystick", "x": 1, "interval_ms": 200})
        self.complete(self.jobs[0])
        self.now += 0.201
        self.assertEqual(self.post({"op": "joystick", "x": -0.4, "interval_ms": 200}).json["status"], "waiting")
        self.now += 3.0
        self.assertEqual(self.post({"op": "joystick", "x": -0.4, "interval_ms": 200}).json["status"], "queued")
        self.assertEqual(self.jobs[-1][1], "b-20,-20")

    def test_joystick_deadzone_change_and_invalid_settings(self):
        self.arm()
        for value in (True, "20", 20.5, 4, 31, float("nan")):
            self.assertEqual(self.post({"op": "joystick", "x": 1, "deadzone_pct": value}).status_code, 400)
        self.assertEqual(self.post({"op": "joystick", "y": 0.3, "deadzone_pct": 30}).json["status"], "neutral")
        self.assertEqual(self.jobs, [])
        self.post({"op": "joystick", "y": 0.3, "deadzone_pct": 5})
        self.assertEqual(self.jobs[0][1], "b20,-20")

    def test_joystick_center_cancels_pending_motion_but_keeps_arm(self):
        self.arm()
        self.post({"op": "joystick", "y": 1})
        first = self.jobs[0]
        self.assertEqual(self.post({"op": "joystick", "y": 0.1}).json["status"], "neutral")
        self.assertFalse(first[2]["allowed"]())
        self.assertTrue(self.state.armed)
        self.assertEqual(self.jobs[-1][1], "x")

    def test_joystick_pending_expiry_and_board_interlocks(self):
        self.arm()
        self.post({"op": "joystick", "y": 1})
        first = self.jobs[0]
        self.assertEqual(self.post({"op": "joystick", "y": -1}).json["status"], "busy")
        self.now += 0.301
        self.assertFalse(first[2]["allowed"]())
        self.complete(first)
        self.sample["boot_id"] += 1
        # Live hardware status is sampled before an eligible command, after
        # cadence gating; waiting polls never send a move to the rebooted board.
        self.now += 0.101
        self.assertEqual(self.post({"op": "joystick", "y": 1}).json["status"], "disarmed")


class VelocityJogTests(RemoteDriveTests):
    """DriveMCU firmware that reports `jogging` gets a velocity stream."""

    def setUp(self):
        super().setUp()
        self.sample.update(jogging=False, x=0, y=0, z=0)
        self.timers = []
        self.state.later = lambda delay, fn: self.timers.append((delay, fn))

    # The pulse-cadence tests above describe old firmware only.
    test_finite_axes_and_single_pending_batch = None
    test_joystick_radial_deadzone_has_no_second_per_axis_deadzone = None
    test_joystick_quadratic_rate_paces_fixed_steps_from_ack = None
    test_joystick_acceleration_does_not_keep_previous_slow_delay = None
    test_joystick_deceleration_and_reversal_obey_current_rate = None
    test_joystick_deadzone_change_and_invalid_settings = None
    test_joystick_center_cancels_pending_motion_but_keeps_arm = None
    test_joystick_pending_expiry_and_board_interlocks = None

    def joy(self, **data):
        data.setdefault("op", "joystick")
        data.setdefault("interval_ms", 100)
        return self.post(data)

    def test_full_stick_streams_velocity_without_waiting_for_motion(self):
        self.arm()
        self.assertEqual(self.joy(y=1).json["status"], "queued")
        self.assertEqual(self.jobs[0][1], "j1000,-1000,0")
        self.complete(self.jobs[0])
        # The board is moving (busy); the next update is still sent at once.
        self.sample.update(busy=True, jogging=True)
        self.now += 0.1
        self.assertEqual(self.joy(y=1).json["status"], "queued")
        self.assertEqual(self.jobs[1][1], "j1000,-1000,0")
        self.assertEqual(self.estimates, [])

    def test_curve_speed_slider_and_rail_scale_velocity(self):
        self.arm()
        self.joy(y=0.6, speed_pct=50)
        self.assertEqual(self.jobs[0][1], "j125,-125,0")
        self.complete(self.jobs[0])
        self.joy(z=-1, interval_ms=200)  # old JC1060: interval 200 ms -> 50 %
        self.assertEqual(self.jobs[1][1], "j0,0,-500")
        for value in (True, "50", 9, 101, 50.5):
            self.assertEqual(self.joy(y=1, speed_pct=value).status_code, 400)

    def test_release_is_decelerating_stop_not_emergency_stop(self):
        self.arm()
        self.joy(x=1)
        self.complete(self.jobs[0])
        self.assertEqual(self.post({"op": "pulse", "x": 0, "y": 0, "z": 0}).json["status"], "neutral")
        self.assertEqual(self.jobs[-1][1], "j0,0,0")
        self.assertNotIn("x", [j[1] for j in self.jobs])
        self.assertTrue(self.state.armed)
        # The settled board position goes to the PC estimate.
        self.sample.update(x=480, y=480)
        delay, run = self.timers[-1]
        run()
        self.assertEqual(self.estimates, ["b480,480"])

    def test_single_in_flight_jog_and_failed_ack_disarms(self):
        self.arm()
        self.joy(y=1)
        self.assertEqual(self.joy(y=1).json["status"], "busy")
        self.assertEqual(len(self.jobs), 1)
        self.complete(self.jobs[0], ok=False)
        self.assertFalse(self.state.armed)

    def test_stale_jog_is_not_sent_after_delay(self):
        self.arm()
        self.joy(y=1)
        self.now += 0.301
        self.assertFalse(self.jobs[0][2]["allowed"]())

    def test_periodic_status_keeps_reboot_pid_and_program_interlocks(self):
        self.arm()
        self.joy(y=1)
        self.complete(self.jobs[0])
        self.sample.update(busy=True, jogging=False)  # a queued program owns the axes
        self.now += 0.2
        self.assertEqual(self.joy(y=1).json["status"], "queued")  # status not due yet
        self.complete(self.jobs[1])
        self.now += 0.4
        self.assertEqual(self.joy(y=1).json["status"], "busy")
        self.sample.update(busy=False)
        self.sample["boot_id"] += 1
        self.assertEqual(self.joy(y=1).json["status"], "disarmed")
        self.assertEqual([j[1] for j in self.jobs[2:]], ["x"])  # rebooted board: stop, no jog

    def test_jog_runs_beside_rail_pid_when_board_reports_program(self):
        # Rail PID on and Z moving: the board is busy but no program owns the axes.
        self.sample.update(program=False, pid=True, busy=True)
        self.arm()
        self.assertEqual(self.joy(y=1).json["status"], "queued")
        self.assertEqual(self.jobs[0][1], "j1000,-1000,0")
        self.complete(self.jobs[0])
        self.now += 0.6  # status due again, PID still on
        self.assertEqual(self.joy(z=1).json["status"], "queued")
        self.assertEqual(self.jobs[1][1], "j0,0,1000")  # the board turns its PID off
        self.assertTrue(self.state.armed)

    def test_program_blocks_arm_and_jog(self):
        self.sample.update(program=True, busy=True)
        self.assertEqual(self.post({"op": "arm"}).status_code, 409)
        self.sample.update(program=False, busy=False)
        self.arm()
        self.joy(y=1)
        self.complete(self.jobs[0])
        self.sample.update(program=True)
        self.now += 0.6
        self.assertEqual(self.joy(y=1).json["status"], "busy")
        self.assertEqual(len(self.jobs), 1)

    def test_status_updates_estimate_with_real_steps_while_jogging(self):
        self.arm()
        self.joy(y=1)
        self.complete(self.jobs[0])
        self.sample.update(busy=True, jogging=True, x=250, y=-250, z=7)
        self.now += 0.5
        self.joy(y=1)
        self.assertEqual(self.estimates, ["b250,-250", "mzr7"])

    def test_rearm_does_not_count_moves_made_by_other_commands(self):
        self.arm()
        self.sample.update(x=1000)
        self.arm()
        self.joy(y=1)
        self.assertEqual(self.estimates, [])

    def test_late_settle_read_ignored_after_pc_takeover(self):
        self.arm()
        self.joy(x=1)
        self.complete(self.jobs[0])
        self.post({"op": "pulse", "x": 0})
        self.client.post("/api/drivemcu/command", json={"command": "mxr50"})
        self.sample.update(x=900)
        self.timers[-1][1]()
        self.assertEqual(self.estimates, [])


class JoystickCurveTests(unittest.TestCase):
    def test_center_boundary_and_rail_are_independent(self):
        self.assertEqual(joystick_rates(0, 0, 1, 20), [0, 0, 1])
        self.assertEqual(joystick_rates(0.2, 0, -1, 20), [0, 0, -1])

    def test_curve_has_fine_center_and_full_edge(self):
        slow = joystick_rates(0, 0.4, 0, 20)
        middle = joystick_rates(0, 0.6, 0, 20)
        edge = joystick_rates(0, 1, 0, 20)
        self.assertAlmostEqual(slow[0], 0.0625)
        self.assertAlmostEqual(middle[0], 0.25)
        self.assertAlmostEqual(edge[0], 1)
        self.assertEqual(edge[1], -1)

    def test_equal_radius_equal_peak_in_every_direction_and_bounded_corners(self):
        import math
        for angle in range(0, 360, 15):
            radians = math.radians(angle)
            axes = joystick_rates(0.6 * math.cos(radians), 0.6 * math.sin(radians), 0, 20)
            self.assertAlmostEqual(max(abs(axes[0]), abs(axes[1])), 0.25)
        self.assertEqual(joystick_rates(1, 1, 0, 20), [1, 0, 0])


if __name__ == "__main__":
    unittest.main()
