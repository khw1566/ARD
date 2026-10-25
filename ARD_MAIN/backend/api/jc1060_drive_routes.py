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


class DriveInput:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.lock = threading.RLock()
        self.generation = 0
        self.armed = False
        self.active = False
        self.pending = 0
        self.boot_id = None
        self.cancel_motion = lambda: None

    def cancel(self, force=False):
        with self.lock:
            self.generation += 1
            self.armed = False
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
                state.armed = True
                return jsonify(status="armed")
            if op != "input":
                return jsonify(status="invalid"), 400
            try:
                values = [data.get(axis, 0) for axis in "xyz"]
                if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or abs(v) > 1 for v in values):
                    raise ValueError()
                steps = [round(v * 20) for v in values]
            except (ValueError, TypeError):
                return jsonify(status="invalid"), 400
            if not any(steps):
                state.generation += 1
                active, state.active = state.active, False
                if active:
                    stop()
                return jsonify(status="neutral")
            if not state.armed:
                return jsonify(status="disarmed"), 409
            if state.pending:
                return jsonify(status="busy"), 202
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
                        accepted(result.command)
            for command in commands:
                dispatcher.submit(ip, 5002, command, callback=complete, allowed=current)
            return jsonify(status="queued"), 202
    return state
