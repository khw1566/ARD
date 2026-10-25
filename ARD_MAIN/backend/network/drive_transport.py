"""Ordered DriveMCU commands with explicit board acknowledgement.

ACK:OK means the firmware accepted a command, not that physical motion finished.
Only connection establishment is retried. Once sending starts, an uncertain
result must never replay a relative move.
"""
from collections import deque
from dataclasses import dataclass, field
import logging
import socket
import threading
import time


@dataclass
class CommandResult:
    command: str
    status: str = "queued"
    message: str = ""
    response: str = ""
    done: threading.Event = field(default_factory=threading.Event, repr=False)

    @property
    def ok(self):
        return self.status == "accepted"


@dataclass
class _Job:
    result: CommandResult
    queued_at: float
    generation: int
    callback: object
    allowed: object


@dataclass
class _Lane:
    pending: deque = field(default_factory=deque)
    generation: int = 0


class DriveCommandDispatcher:
    STOP_COMMANDS = frozenset(("x", "stop", "d", "disable", "w"))

    def __init__(self, connector=socket.create_connection, connect_timeout=0.5,
                 ack_timeout=1.5, connect_attempts=3, max_pending=32, max_age=3.0):
        self.connector = connector
        self.connect_timeout = connect_timeout
        self.ack_timeout = ack_timeout
        self.connect_attempts = connect_attempts
        self.max_pending = max_pending
        self.max_age = max_age
        self.condition = threading.Condition()
        self.lanes = {}
        self.closed = False

    def submit(self, ip, port, command, callback=None, allowed=None):
        result = CommandResult(command)
        try:
            payload = command.encode("ascii")
            valid = ip and ip != "Unknown" and 0 < len(payload) <= 127 and all(32 <= c <= 126 for c in payload)
        except (AttributeError, UnicodeError):
            valid = False
        if not valid:
            self._finish(_Job(result, 0, 0, callback, allowed), "invalid", "올바른 IP와 한 줄 명령이 필요합니다.")
            return result
        cancelled = []
        endpoint = (ip, port)
        urgent = command.strip().lower() in self.STOP_COMMANDS
        with self.condition:
            lane = self.lanes.get(endpoint)
            if lane is None:
                lane = self.lanes[endpoint] = _Lane()
                threading.Thread(target=self._run, args=(endpoint, lane), daemon=True,
                                 name="drive-command").start()
            if urgent:
                lane.generation += 1
                cancelled = list(lane.pending)
                lane.pending.clear()
            job = _Job(result, time.monotonic(), lane.generation, callback, allowed)
            if self.closed:
                error = ("cancelled", "명령 전송기가 종료되었습니다.")
            elif len(lane.pending) >= self.max_pending:
                error = ("busy", "드라이브 명령 대기열이 가득 찼습니다.")
            else:
                error = None
                lane.pending.append(job)
                self.condition.notify_all()
        for old in cancelled:
            self._finish(old, "cancelled", "정지 요청으로 대기 명령을 취소했습니다.")
        if error:
            self._finish(job, *error)
        return result

    def _finish(self, job, status, message, response=""):
        job.result.status, job.result.message, job.result.response = status, message, response
        try:
            if job.callback:
                job.callback(job.result)
        except Exception:
            logging.getLogger(__name__).exception("Drive command result callback failed")
        finally:
            job.result.done.set()

    def _superseded(self, lane, job):
        with self.condition:
            obsolete = self.closed or job.generation != lane.generation
        return obsolete or (job.allowed is not None and not job.allowed())

    def _exchange(self, endpoint, lane, job):
        last_error = "연결 실패"
        for attempt in range(self.connect_attempts):
            if self._superseded(lane, job):
                return "cancelled", "취소되었거나 현재 보드 선택과 다른 명령입니다.", ""
            if job.result.command.strip().lower() not in self.STOP_COMMANDS and time.monotonic() - job.queued_at > self.max_age:
                return "expired", "오래 대기한 명령은 뒤늦게 실행하지 않습니다.", ""
            try:
                connection = self.connector(endpoint, timeout=self.connect_timeout)
            except OSError as exc:
                last_error = str(exc)
                if attempt + 1 < self.connect_attempts:
                    # A stop wakes this wait and invalidates an unsent move.
                    with self.condition:
                        self.condition.wait(0.1)
                continue
            sent = False
            try:
                with connection:
                    connection.settimeout(self.ack_timeout)
                    if self._superseded(lane, job):
                        return "cancelled", "정지 또는 보드 선택 변경으로 전송을 취소했습니다.", ""
                    if job.result.command.strip().lower() not in self.STOP_COMMANDS and time.monotonic() - job.queued_at > self.max_age:
                        return "expired", "오래 대기한 명령은 뒤늦게 실행하지 않습니다.", ""
                    sent = True  # Even a partial send must not replay a relative move.
                    connection.sendall((job.result.command + "\n").encode("ascii"))
                    response = bytearray()
                    deadline = time.monotonic() + self.ack_timeout
                    while b"\n" not in response:
                        if self._superseded(lane, job):
                            return "unconfirmed", "정지 또는 보드 선택 변경으로 수락 확인을 중단했습니다. 재전송하지 않습니다.", ""
                        remaining = deadline - time.monotonic()
                        if remaining <= 0 or len(response) >= 256:
                            raise OSError("ACK 응답 시간 또는 길이 제한을 넘었습니다.")
                        connection.settimeout(min(remaining, 0.05))
                        try:
                            part = connection.recv(256 - len(response))
                        except socket.timeout:
                            continue
                        if not part:
                            raise OSError("ACK 전에 연결이 종료되었습니다.")
                        response.extend(part)
                    reply = bytes(response).split(b"\n", 1)[0].decode("ascii").strip()
                    if reply == "ACK:OK":
                        return "accepted", "DriveMCU가 명령을 수락했습니다.", reply
                    if reply.startswith("ERR:"):
                        return "rejected", "DriveMCU가 명령을 거부했습니다: " + reply, reply
                    return "unconfirmed", "명령 수락을 확인할 수 없는 응답입니다: " + reply, reply
            except (OSError, UnicodeError) as exc:
                if sent:
                    return "unconfirmed", "전송 후 수락 확인 실패. 중복 이동 방지를 위해 재전송하지 않습니다: " + str(exc), ""
                return "offline", str(exc), ""
        return "offline", "DriveMCU 연결 실패: " + last_error, ""

    def _run(self, endpoint, lane):
        while True:
            with self.condition:
                if not lane.pending and not self.closed:
                    self.condition.wait_for(lambda: lane.pending or self.closed, timeout=30)
                if not lane.pending:
                    if self.lanes.get(endpoint) is lane:
                        del self.lanes[endpoint]
                    return
                job = lane.pending.popleft()
            if job.result.command.strip().lower() not in self.STOP_COMMANDS and time.monotonic() - job.queued_at > self.max_age:
                outcome = ("expired", "오래 대기한 명령은 뒤늦게 실행하지 않습니다.", "")
            else:
                try:
                    outcome = self._exchange(endpoint, lane, job)
                except Exception as exc:
                    outcome = ("unconfirmed", str(exc), "")
            self._finish(job, *outcome)

    def close(self):
        with self.condition:
            self.closed = True
            pending = [job for lane in self.lanes.values() for job in lane.pending]
            for lane in self.lanes.values():
                lane.pending.clear()
            self.condition.notify_all()
        for job in pending:
            self._finish(job, "cancelled", "명령 전송기가 종료되었습니다.")


drive_commands = DriveCommandDispatcher()
