"""Latest DriveMCU sensor/rail state, read from the board's TCP `status`.

The DriveMCU heartbeat (HTTP, default 1 s) only carries tof_dist. The board's
`status` answer also carries the rail PID state (pid, rail_target,
rail_settled), so the PC reads it about every 300 ms and gives the browser and
the JC1060 the board's real values instead of the last command the PC sent.

Every other `status` read (jog, rail tools) is recorded too, and the poll is
skipped while such a read is fresh, so the board's one-connection-at-a-time
command server sees no extra load during a jog or a rail tool run.
"""
import json
import threading
import time

PERIOD_S = 0.3        # poll period while DriveMCU is online
STALE_S = 2.0         # older samples are reported as not live
KEYS = ('tof_mm', 'tof_ok', 'tof_status', 'pid', 'rail_target', 'rail_dir', 'rail_settled',
        'busy', 'stopped', 'jogging', 'x', 'y', 'z', 'boot_id', 'steps_per_mm', 'pid_p', 'pid_i', 'pid_d',
        'rail_limit_on', 'rail_min', 'rail_max', 'rail_limit_count', 'rail_limit')


class DriveLive:
    def __init__(self, read_status, get_ip, enabled, publish, period=PERIOD_S, clock=time.monotonic):
        self.read_status = read_status  # ip -> status JSON text
        self.get_ip = get_ip
        self.enabled = enabled          # () -> bool: poll only while the board is online and idle for others
        self.publish = publish          # (snapshot, changed_keys) -> None
        self.period = period
        self.clock = clock
        self.lock = threading.Lock()
        self.state = {}
        self.sampled_at = None
        self.failures = 0
        self.unsupported_ip = None      # answered `status` without protocol 1 (older firmware)

    def record(self, sample):
        """Store one parsed `status` answer. Returns the keys that changed."""
        if not isinstance(sample, dict) or sample.get('protocol') != 1:
            return set()
        values = {key: sample.get(key) for key in KEYS if key in sample}
        with self.lock:
            changed = {key for key, value in values.items() if self.state.get(key) != value}
            self.state.update(values)
            self.sampled_at = self.clock()
            self.failures = 0
            snapshot = self._snapshot()
        if changed:
            self.publish(snapshot, changed)
        return changed

    def record_text(self, text):
        try:
            sample = json.loads(text)
        except (TypeError, ValueError):
            sample = None
        self.record(sample)
        return text

    def _snapshot(self):
        age = None if self.sampled_at is None else self.clock() - self.sampled_at
        live = age is not None and age <= STALE_S
        return {**self.state, 'live': live, 'age_ms': None if age is None else int(age * 1000)}

    def board_live(self):
        """True while a recent `status` carries the board's own step positions."""
        snap = self.snapshot()
        return bool(snap['live']) and all(isinstance(snap.get(k), (int, float)) for k in ('x', 'y', 'z'))

    def snapshot(self):
        with self.lock:
            return self._snapshot()

    def poll_once(self):
        with self.lock:
            fresh = self.sampled_at is not None and self.clock() - self.sampled_at < self.period
        if fresh or not self.enabled():
            return False
        ip = self.get_ip()
        if not ip or ip == 'Unknown' or ip == self.unsupported_ip:
            return False
        try:
            text = self.read_status(ip)
        except (OSError, RuntimeError, ValueError):
            with self.lock:
                self.failures += 1
            return False
        try:
            sample = json.loads(text)
        except (TypeError, ValueError):
            sample = None
        if not isinstance(sample, dict) or sample.get('protocol') != 1:
            # Never keep sending an unknown line to firmware that may parse it as a command.
            self.unsupported_ip = ip
            return False
        self.record(sample)
        return True

    def run(self, stop=None):
        while stop is None or not stop.is_set():
            self.poll_once()
            # Back off on an unreachable board instead of piling up 250 ms timeouts.
            time.sleep(self.period * min(1 + self.failures, 10))

    def start(self):
        threading.Thread(target=self.run, daemon=True, name='drive-live').start()
