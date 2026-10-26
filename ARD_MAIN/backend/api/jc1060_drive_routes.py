"""Finite remote jog pulses using the unchanged DriveMCU command protocol.

There is one in-flight batch, no input FIFO, and unsent pulses expire after
300 ms. An issued pulse is at most 20 steps per axis; network loss cannot
cancel steps already accepted by the physical board.
"""
import math
import json
import threading
import time
from flask import jsonify, request
from core.sorting_pipeline import request_line


def joystick_rates(rotation, forward, rail, deadzone_pct):
    """Radial deadzone and quadratic rate, followed by differential mixing.

    x is right/clockwise, y is forward. The dominant disk axis reaches the
    same rate at every angle. Rates pace fixed 20-step commands.
    """
    radius = math.hypot(rotation, forward)
    deadzone = deadzone_pct / 100.0
    if radius <= deadzone:
        return [0.0, 0.0, rail]
    magnitude = ((min(radius, 1.0) - deadzone) / (1.0 - deadzone)) ** 2
    a, b = forward + rotation, -forward + rotation
    peak = max(abs(a), abs(b))
    return [a / peak * magnitude, b / peak * magnitude, rail]


class DriveInput:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.lock = threading.RLock()
        self.generation = 0
        self.armed = False
        self.active = False
        self.pending = 0
        self.boot_id = None
        self.axis_due = [0.0, 0.0, 0.0]
        self.axis_direction = [0, 0, 0]
        self.axis_last = [None, None, None]
        self.cancel_motion = lambda: None

    def cancel(self, force=False):
        with self.lock:
            self.generation += 1
            self.armed = False
            self.axis_due = [0.0, 0.0, 0.0]
            self.axis_direction = [0, 0, 0]
            self.axis_last = [None, None, None]
            active, self.active = self.active, False
        if active or force:
            self.cancel_motion()


def register_jc1060_drive_routes(app, boards, jc_ip, target_ip, dispatcher, sorting, accepted,
                                 read_status=None):
    state = DriveInput()
    if read_status is None:
        read_status = lambda ip: json.loads(request_line(ip, "status", timeout=0.25))

    def ready_status(ip):
        sample = read_status(ip)
        if not isinstance(sample, dict) or sample.get("protocol") != 1 or not all(key in sample for key in ("boot_id", "busy", "pid")):
            raise ValueError("DriveMCU status protocol 1 required")
        return sample

    def allowed_peer():
        return boards.snapshot()["control_terminal"] == "jc1060" and bool(jc_ip()) and request.remote_addr == jc_ip()

    def destination():
        ip = target_ip()
        return ip if ip and ip != "Unknown" and boards.accepts("drivemcu", {}, ip, jc_ip()) else None

    def stop():
        ip = destination()
        if ip:
            dispatcher.submit(ip, 5002, "x")
    state.cancel_motion = stop

    @app.before_request
    def manual_override():
        if request.method == "POST" and request.path in (
                "/api/drivemcu/command", "/api/sorting/start", "/api/hardware/config"):
            state.cancel()

    @app.post("/api/jc1060/drive")
    def remote_input():
        if not allowed_peer():
            return jsonify(status="inactive"), 403
        data = request.get_json(silent=True)
        if not isinstance(data, dict):
            return jsonify(status="invalid"), 400
        op = data.get("op", "input")
        if op == "disarm":
            # Leaving the manual page must not cancel an independent sorting
            # sequence. Only an explicit E-stop cancels both operations.
            state.cancel()
            return jsonify(status="disarmed")
        if op == "stop":
            state.cancel(force=True)
            if sorting().active:
                sorting().cancelled.set()
            return jsonify(status="stopped")
        ip = destination()
        if not ip:
            state.cancel()
            return jsonify(status="offline"), 503
        if sorting().active:
            state.cancel()
            return jsonify(status="disarmed"), 409
        with state.lock:
            if op == "arm":
                try:
                    sample = ready_status(ip)
                except (OSError, ValueError, RuntimeError):
                    state.armed = False
                    return jsonify(status="offline"), 503
                if sample["busy"] or sample["pid"]:
                    return jsonify(status="busy"), 409
                state.boot_id = sample["boot_id"]
                state.generation += 1
                state.active = False
                state.armed = True
                state.axis_due = [0.0, 0.0, 0.0]
                state.axis_direction = [0, 0, 0]
                state.axis_last = [None, None, None]
                return jsonify(status="armed")
            if op not in ("input", "pulse", "joystick"):
                return jsonify(status="invalid"), 400
            try:
                values = [data.get(axis, 0) for axis in "xyz"]
                if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or abs(v) > 1 for v in values):
                    raise ValueError()
                steps = [round(v * 20) for v in values]
                interval = data.get("interval_ms", 100)
                if op in ("pulse", "joystick"):
                    if isinstance(interval, bool) or not isinstance(interval, int) or not 100 <= interval <= 1000:
                        raise ValueError()
                    # Pulse size stays fixed. Deflection changes each axis's
                    # command interval, never the DriveMCU speed settings.
                    threshold = 0.12
                    if op == "joystick":
                        deadzone = data.get("deadzone_pct", 20)
                        if isinstance(deadzone, bool) or not isinstance(deadzone, int) or not 5 <= deadzone <= 30:
                            raise ValueError()
                        values = joystick_rates(*values, deadzone)
                        # A second per-axis deadzone would erase slow input.
                        threshold = 1e-6
                    steps = [20 if v > threshold else -20 if v < -threshold else 0 for v in values]
            except (ValueError, TypeError):
                return jsonify(status="invalid"), 400
            if not any(steps):
                state.generation += 1
                state.axis_due = [0.0, 0.0, 0.0]
                state.axis_direction = [0, 0, 0]
                state.axis_last = [None, None, None]
                active, state.active = state.active, False
                if active:
                    stop()
                return jsonify(status="neutral")
            if not state.armed:
                return jsonify(status="disarmed"), 409
            if state.pending:
                return jsonify(status="busy"), 202
            now = state.clock()
            if op in ("pulse", "joystick"):
                for axis, step in enumerate(steps):
                    direction = (step > 0) - (step < 0)
                    if direction != state.axis_direction[axis]:
                        if direction == 0:
                            state.axis_due[axis] = 0.0
                            state.axis_last[axis] = None
                        state.axis_direction[axis] = direction
                    if op == "joystick" and step and state.axis_last[axis] is not None:
                        # Use the CURRENT rate and the last ACK. Increasing
                        # deflection must shorten a previous slow input's wait.
                        state.axis_due[axis] = state.axis_last[axis] + interval / 1000.0 / abs(values[axis])
                    if now < state.axis_due[axis]:
                        steps[axis] = 0
                if not any(steps):
                    return jsonify(status="waiting"), 202
            try:
                sample = ready_status(ip)
            except (OSError, ValueError, RuntimeError):
                state.cancel()
                return jsonify(status="offline"), 503
            if sample["boot_id"] != state.boot_id or sample["pid"]:
                state.cancel()
                return jsonify(status="disarmed"), 409
            # Acceptance is not completion: never accumulate relative moves in
            # the physical board's planner while the previous batch is moving.
            if sample["busy"]:
                return jsonify(status="busy"), 202
            if op in ("pulse", "joystick"):
                for axis, step in enumerate(steps):
                    if step:
                        state.axis_last[axis] = state.clock()
                        state.axis_due[axis] = state.axis_last[axis] + interval / 1000.0 / abs(values[axis])
            commands = []
            if steps[0] or steps[1]:
                commands.append(f"b{steps[0]},{steps[1]}")
            if steps[2]:
                commands.append(f"mzr{steps[2]}")
            generation = state.generation
            deadline = state.clock() + 0.3
            state.pending = len(commands)
            state.active = True

            def current():
                with state.lock:
                    return (state.armed and generation == state.generation and state.clock() < deadline
                            and not sorting().active and ip == destination())

            def complete(result):
                with state.lock:
                    state.pending -= 1
                    if not result.ok and generation == state.generation:
                        state.armed = False
                        state.generation += 1
                    if result.ok:
                        if op in ("pulse", "joystick") and generation == state.generation:
                            axes = (0, 1) if result.command.startswith("b") else (2,)
                            for axis in axes:
                                if steps[axis]:
                                    state.axis_last[axis] = state.clock()
                                    state.axis_due[axis] = state.axis_last[axis] + interval / 1000.0 / abs(values[axis])
                        accepted(result.command)
            for command in commands:
                dispatcher.submit(ip, 5002, command, callback=complete, allowed=current)
            return jsonify(status="queued"), 202
    return state
