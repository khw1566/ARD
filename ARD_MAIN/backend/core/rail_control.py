"""Z rail ToF PID: calibration file, board parameters, measurement and leveling tools.

The rail went back from open-loop steps to the DriveMCU ToF PID on 2026-10-26 and is
now mounted mirrored, on the opposite side. Two things changed with the mount:

* rail_dir: the sign between +Z steps and the ToF distance (DriveMCU `mzv`).
  "measure_direction" moves the rail a known number of steps and reads it back.
* the ToF distances of the rail center and of the two bin sides. They are taught on
  the real rig ("teach") instead of being derived from the original design.

The sorting sequence refuses the PID rail until all three positions are taught.
"""
import json
import math
from pathlib import Path
import threading
import time

SLOTS = ('center', 'plus', 'minus')  # plus: +15 mm bins, minus: -15 mm bins
CATEGORIES = ('Metal', 'Glass', 'Others', 'Paper', 'Plastic')
TILTS = ('forward', 'backward')
# Sorting uses only the rail and a forward/backward tray tilt (no disk turn) since 2026-10-27.
# Guessed from the old yaw layout (yaw 90 -> forward, 270 -> backward); check on the rig
# with the drive-tab "배출 위치 시험" and correct the table there.
DEFAULT_DESTINATIONS = {
    'Metal': {'rail': 'plus', 'tilt': 'forward'},
    'Glass': {'rail': 'minus', 'tilt': 'forward'},
    'Others': {'rail': 'minus', 'tilt': 'forward'},
    'Paper': {'rail': 'plus', 'tilt': 'backward'},
    'Plastic': {'rail': 'minus', 'tilt': 'backward'},
}
CATEGORY_NAMES = {'Metal': '금속/캔', 'Glass': '유리', 'Others': '기타', 'Paper': '종이/팩', 'Plastic': '플라스틱'}
SLOT_NAMES = {'center': '중앙', 'plus': '+측', 'minus': '-측'}
TILT_NAMES = {'forward': '전진 전도', 'backward': '후진 전도'}
RAIL_MM = {'plus': 15.0, 'center': 0.0, 'minus': -15.0}  # open-loop rail offsets from the start
DEFAULTS = {
    'mode': 'pid',          # 'pid' (ToF target) or 'open_loop' (steps from the start position)
    # ToF distances (mm). Mirrored mount: the +15 mm side is now the shorter distance.
    'center_mm': 95.0, 'plus_mm': 70.0, 'minus_mm': 120.0,
    # Sequence origin (user spec 2026-10-27): tray level (X/Y 0) and rail at this ToF distance.
    # It is also the camera position. Not taught: the user gave the value. capture_mm is the
    # older name of the same setting and is kept equal to home_mm for older screens/firmware.
    'home_mm': 120.0,
    'capture_mm': 120.0,
    # Z safety limits (user spec 2026-10-27): the rail works between these ToF distances.
    # Pushed to the DriveMCU (mzl), which stops any Z motion before it leaves the range.
    'rail_min_mm': 70.0, 'rail_max_mm': 120.0,
    'taught': {slot: False for slot in SLOTS},
    'rail_dir': -1,         # +Z steps shorten the ToF distance (mirrored mount)
    'steps_per_mm': 43.48,
    'pid_p': 0.12, 'pid_i': 0.0, 'pid_d': 0.0,  # conservative initial tuning, matches DriveMCU
    'settle_s': 0.3,        # rail_settled must hold this long
    'timeout_s': 15.0,
    'destinations': DEFAULT_DESTINATIONS,
    'tilt_deg': 45.0,       # tray tilt for dumping
}
SPAN_MARGIN_MM = 10.0       # manual targets may leave the taught span by this much
# Disc tilt safety limit (user spec 2026-10-27). The DriveMCU enforces the same limit on every
# move, jog and homing; disc rotation is exempt.
TILT_LIMIT_DEG = 60.0


def _number(value, name, low, high):
    if isinstance(value, bool):
        raise ValueError(f'{name} 값이 숫자가 아닙니다.')
    try:
        value = float(value)
    except (TypeError, ValueError):
        raise ValueError(f'{name} 값이 숫자가 아닙니다.') from None
    if not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f'{name} 값은 {low:g}~{high:g} 범위여야 합니다.')
    return value


def validate(changes, base=None):
    if not isinstance(changes, dict):
        raise ValueError('레일 설정은 JSON 객체여야 합니다.')
    unknown = set(changes) - set(DEFAULTS)
    if unknown:
        raise ValueError(f'지원하지 않는 레일 설정입니다: {", ".join(sorted(unknown))}')
    out = json.loads(json.dumps(base or DEFAULTS))
    if 'capture_mm' in changes and 'home_mm' not in changes:
        changes = {**changes, 'home_mm': changes['capture_mm']}  # older name of the origin
    for key, value in changes.items():
        if key == 'mode':
            if value not in ('pid', 'open_loop'):
                raise ValueError('레일 구동 방식은 pid 또는 open_loop이어야 합니다.')
            out[key] = value
        elif key == 'taught':
            if not isinstance(value, dict) or set(value) - set(SLOTS) or any(type(v) is not bool for v in value.values()):
                raise ValueError('taught 값이 올바르지 않습니다.')
            out[key].update(value)
        elif key == 'destinations':
            if not isinstance(value, dict) or set(value) - set(CATEGORIES):
                raise ValueError(f'배출 위치 표의 재질은 {", ".join(CATEGORIES)} 중 하나여야 합니다.')
            for category, dest in value.items():
                if not isinstance(dest, dict) or set(dest) - {'rail', 'tilt'}:
                    raise ValueError(f'{category} 배출 위치는 rail, tilt 값만 가질 수 있습니다.')
                merged = {**out[key][category], **dest}
                if merged['rail'] not in SLOTS:
                    raise ValueError(f'{category} 레일 위치는 plus, center, minus 중 하나여야 합니다.')
                if merged['tilt'] not in TILTS:
                    raise ValueError(f'{category} 전도 방향은 forward 또는 backward여야 합니다.')
                out[key][category] = merged
        elif key == 'tilt_deg':
            out[key] = round(_number(value, '전도 각도', 5, TILT_LIMIT_DEG), 1)
        elif key == 'rail_dir':
            if value not in (1, -1) or isinstance(value, bool):
                raise ValueError('레일 방향은 1 또는 -1이어야 합니다.')
            out[key] = int(value)
        elif key.endswith('_mm') and key != 'steps_per_mm':
            out[key] = round(_number(value, key, 1, 2000), 1)
        elif key == 'steps_per_mm':
            out[key] = _number(value, key, 0.000001, 100000)
        elif key.startswith('pid_'):
            out[key] = _number(value, key, 0, 10000)
        elif key == 'settle_s':
            out[key] = _number(value, key, 0, 5)
        elif key == 'timeout_s':
            out[key] = _number(value, key, 1, 120)
    out['capture_mm'] = out['home_mm']
    out['rail_min_mm'], out['rail_max_mm'] = float(round(out['rail_min_mm'])), float(round(out['rail_max_mm']))
    if out['rail_max_mm'] - out['rail_min_mm'] < 10:
        raise ValueError('Z축 안전 한계는 최대가 최소보다 10mm 이상 커야 합니다.')
    for key, name in (('home_mm', '원점'), ('center_mm', '중앙'), ('plus_mm', '+측'), ('minus_mm', '-측')):
        if not out['rail_min_mm'] <= out[key] <= out['rail_max_mm']:
            raise ValueError(f"{name} {out[key]:g}mm가 Z축 안전 한계 {out['rail_min_mm']:g}~{out['rail_max_mm']:g}mm 밖입니다.")
    return out


def require_in_limits(config, target_mm, what='목표'):
    lo, hi = config.get('rail_min_mm', DEFAULTS['rail_min_mm']), config.get('rail_max_mm', DEFAULTS['rail_max_mm'])
    if not lo <= float(target_mm) <= hi:
        raise RuntimeError(f'{what} {float(target_mm):g}mm는 Z축 안전 한계 {lo:g}~{hi:g}mm 밖입니다.')


def open_loop_steps(config, target_mm, home_mm, steps_per_mm):
    """Open-loop rail steps from the origin to a ToF target: (target - home) x steps/mm x rail_dir."""
    require_in_limits(config, target_mm)
    return round((float(target_mm) - float(home_mm)) * float(steps_per_mm) * config['rail_dir'])


def destination(config, category):
    """Rail slot, open-loop offset and tilt of one material from the stored table."""
    config = config or DEFAULTS
    dest = config.get('destinations', DEFAULT_DESTINATIONS).get(category) or DEFAULT_DESTINATIONS[category]
    sign = 1 if dest['tilt'] == 'forward' else -1
    deg = float(config.get('tilt_deg', DEFAULTS['tilt_deg']))
    return {'category': category, 'rail': dest['rail'], 'rail_mm': RAIL_MM[dest['rail']], 'tilt': dest['tilt'],
            'tilt_sign': sign, 'tilt_deg': deg, 'roll': sign * deg,
            'name': f"{CATEGORY_NAMES.get(category, category)} ({SLOT_NAMES[dest['rail']]} · {TILT_NAMES[dest['tilt']]})"}


def tilt_steps(roll_deg, ratios):
    """Tray tilt as the differential pair: forward = +X / -Y motor steps (df), backward negated."""
    if not abs(float(roll_deg)) <= TILT_LIMIT_DEG:
        raise RuntimeError(f'전도 각도 {float(roll_deg):g}°는 디스크 안전 한계 ±{TILT_LIMIT_DEG:g}° 밖입니다.')
    gx, gy = ratios
    return round(roll_deg / 1.8 * gx), -round(roll_deg / 1.8 * gy)


def disc_command_steps(cmd):
    """X/Y motor steps of a DriveMCU disc move command (df/db/dr/dl, b<x>,<y>, mxr, myr), else None."""
    cmd = cmd.strip().lower()
    try:
        if cmd[:2] in ('df', 'db', 'dr', 'dl'):
            steps = int(cmd[2:] or '200') or 200
            sx, sy = {'df': (1, -1), 'db': (-1, 1), 'dr': (1, 1), 'dl': (-1, -1)}[cmd[:2]]
            return sx * steps, sy * steps
        if cmd.startswith('b') and not cmd.startswith('bv') and ',' in cmd:
            x, y = cmd[1:].split(',')
            return int(x), int(y)
        if cmd.startswith('mxr'):
            return int(cmd[3:]), 0
        if cmd.startswith('myr'):
            return 0, int(cmd[3:])
    except ValueError:
        return None  # malformed: the DriveMCU rejects it
    return None


def disc_tilt_deg(x, y, ratios):
    """Disc tilt (deg) of motor positions: (a - b) / 2 with a, b the reducer outputs."""
    gx, gy = ratios
    return (x * 1.8 / gx - y * 1.8 / gy) / 2


def disc_tilt_error(cmd, x, y, ratios):
    """Message when a disc move would leave the tilt limit, else None. Rotation is not limited.
    A move back toward level from outside the limit stays allowed (same rule as the DriveMCU)."""
    steps = disc_command_steps(cmd)
    if steps is None:
        return None
    before = disc_tilt_deg(x, y, ratios)
    after = disc_tilt_deg(x + steps[0], y + steps[1], ratios)
    if abs(after) <= TILT_LIMIT_DEG or (after * before > 0 and abs(after) < abs(before)):
        return None
    return (f'디스크 기울기가 {after:+.1f}°가 되어 안전 한계 ±{TILT_LIMIT_DEG:g}°를 넘습니다. '
            '디스크 회전은 제한하지 않습니다.')


def bin_target(config, rail_mm):
    """ToF target of a sorting destination given in design millimetres (+15 / -15 / 0)."""
    if rail_mm > 0:
        return config['plus_mm']
    if rail_mm < 0:
        return config['minus_mm']
    return config['center_mm']


def require_taught(config):
    missing = [name for slot, name in (('center', '중앙'), ('plus', '+측'), ('minus', '-측'))
               if not config['taught'].get(slot)]
    if missing:
        raise RuntimeError(f'레일 위치 티칭이 필요합니다({", ".join(missing)}). '
                           '구동 보드 탭의 레일 측정·레벨링 도구에서 저장하세요.')


def require_slots_taught(config, slots):
    """Bin moves need their ToF targets; the origin (home_mm) does not."""
    missing = [SLOT_NAMES[s] for s in SLOTS if s in slots and not config['taught'].get(s)]
    if missing:
        raise RuntimeError(f'배출 위치의 레일 티칭이 필요합니다({", ".join(missing)}). '
                           '구동 보드 탭의 레일 측정·레벨링 도구에서 저장하세요.')


def tof_pid_ready(state):
    """The board can run the ToF PID: rail firmware, a valid ToF, not stopped."""
    return ('rail_settled' in state and 'rail_dir' in state and bool(state.get('tof_ok'))
            and not state.get('stopped'))


def require_rail_firmware(state):
    if 'rail_settled' not in state or 'rail_dir' not in state:
        raise RuntimeError('DriveMCU 펌웨어가 레일 PID 상태를 보고하지 않습니다. 업데이트된 펌웨어를 업로드하세요.')


def parameter_commands(config):
    """Board commands for the stored calibration. mzv is rejected while the PID runs."""
    return [f"mzv{config['rail_dir']}", f"mzs{config['steps_per_mm']:.4f}",
            f"mzp{config['pid_p']:g}", f"mzi{config['pid_i']:g}", f"mzd{config['pid_d']:g}",
            f"mzl{int(config.get('rail_min_mm', 70))},{int(config.get('rail_max_mm', 120))}"]


def pid_move(command, status, target_mm, config, cancelled, clock=time.monotonic, hold=False):
    """Set the PID target, wait for the board's settled flag, then (unless hold) PID off.

    command(cmd) raises when the board does not acknowledge. status() returns the JSON.
    """
    target = int(round(target_mm))
    require_in_limits(config, target, 'PID 목표')
    start = clock()
    command(f'mzt{target}')
    command('mzm1')
    settled_since = None
    try:
        while True:
            if cancelled.is_set():
                raise RuntimeError('레일 이동이 중단되었습니다.')
            state = status()
            require_rail_firmware(state)
            if state.get('stopped'):
                raise RuntimeError('비상정지로 레일 PID가 중단되었습니다.')
            if not state.get('pid'):
                raise RuntimeError('레일 PID가 스스로 꺼졌습니다. 오차가 커지는 방향으로 움직였거나 ToF가 끊겼습니다. '
                                   '레일 방향 자동 측정을 다시 실행하세요.')
            now = clock()
            if state.get('rail_settled'):
                if settled_since is None:
                    settled_since = now
                if now - settled_since >= config['settle_s']:
                    break
            else:
                settled_since = None
            if now - start > config['timeout_s']:
                raise RuntimeError(f"레일이 {config['timeout_s']:g}초 안에 목표 {target}mm에 정착하지 못했습니다 "
                                   f"(현재 ToF {float(state.get('tof_mm', 0)):.1f}mm).")
            cancelled.wait(.1)
    except Exception:
        try:
            command('mzm0')
        except Exception:
            pass
        raise
    if not hold:
        command('mzm0')
    tof = float(state.get('tof_mm', 0))
    return {'target_mm': target, 'tof_mm': round(tof, 1), 'error_mm': round(tof - target, 1),
            'elapsed_s': round(clock() - start, 2), 'z': state.get('z')}


def statistics(values):
    n = len(values)
    mean = sum(values) / n
    std = math.sqrt(sum((v - mean) ** 2 for v in values) / (n - 1)) if n > 1 else 0.
    return {'n': n, 'mean_mm': round(mean, 2), 'std_mm': round(std, 2),
            'min_mm': round(min(values), 1), 'max_mm': round(max(values), 1),
            'span_mm': round(max(values) - min(values), 1)}


class RailTools:
    """Measurement and leveling tools. One tool runs at a time; cancel() ends it."""

    def __init__(self, path, transport, clock=time.monotonic):
        self.path = Path(path)
        self.transport, self.clock = transport, clock
        self.lock = threading.Lock()          # file and config
        self.running = threading.Lock()       # one tool at a time
        self.cancelled = threading.Event()
        self.config = validate({})
        if self.path.exists():
            try:
                self.config = validate(json.loads(self.path.read_text(encoding='utf-8')))
            except (OSError, ValueError, TypeError) as exc:
                print(f'[Rail] Cannot load {self.path.name}: {exc}; using defaults')

    # --- configuration -------------------------------------------------
    def snapshot(self):
        with self.lock:
            return json.loads(json.dumps(self.config))

    def update(self, changes):
        with self.lock:
            candidate = validate(changes, self.config)
            tmp = self.path.with_suffix('.tmp')
            tmp.write_text(json.dumps(candidate, ensure_ascii=False, indent=2), encoding='utf-8')
            tmp.replace(self.path)
            self.config = candidate
            return json.loads(json.dumps(candidate))

    # --- board access --------------------------------------------------
    def command(self, ip, cmd):
        response = self.transport(ip, cmd)
        if response != 'ACK:OK':
            raise RuntimeError(f'DriveMCU가 {cmd} 명령을 거부했습니다({response}).')

    def status(self, ip):
        try:
            state = json.loads(self.transport(ip, 'status'))
            if not isinstance(state, dict) or state.get('protocol') != 1:
                raise ValueError()
        except (ValueError, TypeError):
            raise RuntimeError('DriveMCU 상태 조회 미지원: 업데이트된 펌웨어를 업로드하세요.') from None
        return state

    def live(self, ip):
        state = self.status(ip)
        keys = ('pid', 'busy', 'stopped', 'jogging', 'z', 'steps_per_mm', 'tof_mm', 'tof_ok',
                'tof_status', 'rail_target', 'rail_dir', 'rail_settled', 'pid_p', 'pid_i', 'pid_d')
        return {key: state.get(key) for key in keys}

    def apply_pid(self, ip, changes):
        keys = ('pid_p', 'pid_i', 'pid_d')
        if not isinstance(changes, dict) or set(changes) != set(keys):
            raise ValueError('P, I, D 세 값을 모두 입력하세요.')
        candidate = validate(changes, self.snapshot())
        state = self.status(ip)
        if state.get('pid') or state.get('busy') or state.get('jogging'):
            raise RuntimeError('PID를 끄고 모터가 정지한 뒤 계수를 적용하세요.')
        if any(not isinstance(state.get(key), (int, float)) for key in keys):
            raise RuntimeError('현재 계수 조회를 지원하는 DriveMCU 펌웨어가 필요합니다.')
        boot = state.get('boot_id')
        for key, prefix in zip(keys, ('mzp', 'mzi', 'mzd')):
            self.command(ip, f'{prefix}{candidate[key]:g}')
        actual = self.status(ip)
        if actual.get('boot_id') != boot or any(
                not isinstance(actual.get(key), (int, float)) or
                not math.isclose(actual[key], candidate[key], rel_tol=1e-5, abs_tol=1e-6) for key in keys):
            raise RuntimeError('보드 PID 계수 확인에 실패했습니다. 현재값을 새로 읽어 확인하세요.')
        # Persist only after all commands ACK and readback confirms the board values.
        self.update({key: actual[key] for key in keys})
        return {key: actual[key] for key in keys}

    def terminal_control(self, ip, data):
        """JC1060 rail controls. ACK means accepted, never physical completion."""
        op = data.get('op')
        config = self.snapshot()
        if op == 'mode':
            mode = data.get('mode')
            if mode not in ('pid', 'open_loop'):
                raise ValueError('레일 모드는 pid 또는 open_loop이어야 합니다.')
            state = self.status(ip)
            if (state.get('busy') or state.get('jogging')) and not state.get('pid'):
                raise RuntimeError('수동 이동이 끝난 뒤 레일 모드를 전환하세요.')
            self.command(ip, 'mzm0')
            self.update({'mode': mode})
        elif op == 'stop':
            self.command(ip, 'mzm0')
            self.command(ip, 'mzx')
        elif op == 'target':
            if config['mode'] != 'pid':
                raise RuntimeError('PID 모드로 전환한 뒤 목표 이동을 시작하세요.')
            require_taught(config)
            target = self.target_of(mm=data.get('mm'))
            state = self.status(ip)
            require_rail_firmware(state)
            if state.get('stopped') or not state.get('tof_ok') or state.get('jogging'):
                raise RuntimeError('비상정지·조그 상태 또는 ToF 오류를 확인하세요.')
            if not state.get('pid'):
                self._idle_state(ip)
                for cmd in parameter_commands(config):
                    self.command(ip, cmd)
            self.command(ip, f'mzt{int(round(target))}')
            self.command(ip, 'mzm1')
        elif op == 'step':
            if config['mode'] != 'open_loop':
                raise RuntimeError('개루프 모드에서만 스텝 이동할 수 있습니다.')
            steps = data.get('steps')
            if isinstance(steps, bool) or not isinstance(steps, int) or not 1 <= abs(steps) <= 2000:
                raise ValueError('이동량은 1~2000 스텝의 정수여야 합니다.')
            self._idle_state(ip, need_tof=False)
            self.command(ip, f'mzr{steps}')
        else:
            raise ValueError('지원하지 않는 레일 조작입니다.')
        return self.live(ip)

    def apply(self, ip):
        state = self.status(ip)
        require_rail_firmware(state)
        if state.get('pid'):
            raise RuntimeError('레일 PID를 끈 뒤 보정값을 보드에 적용하세요.')
        config = self.snapshot()
        for cmd in parameter_commands(config):
            self.command(ip, cmd)
        return config

    # --- tool runner ---------------------------------------------------
    def run(self, fn, *args):
        if not self.running.acquire(blocking=False):
            raise BlockingIOError('다른 레일 도구가 실행 중입니다.')
        self.cancelled.clear()
        try:
            return fn(*args)
        finally:
            self.running.release()

    @property
    def busy(self):
        return self.running.locked()

    def cancel(self):
        self.cancelled.set()

    def _check(self):
        if self.cancelled.is_set():
            raise RuntimeError('레일 도구가 중단되었습니다.')

    def _idle_state(self, ip, need_tof=True):
        state = self.status(ip)
        require_rail_firmware(state)
        if state.get('busy') or state.get('pid') or state.get('jogging'):
            raise RuntimeError('모터가 멈추고 레일 PID가 꺼진 상태에서 실행하세요.')
        if need_tof and not state.get('tof_ok'):
            raise RuntimeError(f"ToF 측정값이 없습니다(상태: {state.get('tof_status', '알 수 없음')}).")
        return state

    def _sample(self, ip, seconds, period=.1):
        values, deadline = [], self.clock() + seconds
        while True:
            self._check()
            state = self.status(ip)
            if state.get('tof_ok'):
                values.append(float(state['tof_mm']))
            if self.clock() >= deadline:
                break
            self.cancelled.wait(period)
        if len(values) < 3:
            raise RuntimeError('유효한 ToF 측정값이 부족합니다. 센서 배선과 측정 거리를 확인하세요.')
        return statistics(values)

    def _wait_idle(self, ip, timeout=30):
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            self._check()
            state = self.status(ip)
            if state.get('stopped'):
                raise RuntimeError('비상정지가 감지되었습니다.')
            if not state.get('busy'):
                self.cancelled.wait(.4)  # the board averages 5 ToF samples (250 ms)
                return state
            self.cancelled.wait(.1)
        raise RuntimeError('레일 이동 완료 확인 시간이 초과되었습니다.')

    # --- tools ---------------------------------------------------------
    def sample_tof(self, ip, seconds=3.0):
        """ToF stability at rest: mean, standard deviation and spread of the filtered value."""
        self._idle_state(ip)
        return self._sample(ip, seconds)

    def measure_direction(self, ip, steps=400, apply=False):
        """Open-loop +steps and back, read the ToF change: rail_dir and steps/mm."""
        steps = int(steps)
        if not 20 <= abs(steps) <= 4000:
            raise ValueError('측정 스텝은 20~4000이어야 합니다.')
        self._idle_state(ip)
        before = self._sample(ip, 1.0)
        self.command(ip, f'mzr{steps}')
        self._wait_idle(ip)
        after = self._sample(ip, 1.0)
        # Back to the start position. Not after an error: a move command re-arms E-STOP.
        self.command(ip, f'mzr{-steps}')
        self._wait_idle(ip)
        back = self._sample(ip, 1.0)
        delta = after['mean_mm'] - before['mean_mm']
        noise = max(before['std_mm'], after['std_mm'])
        if abs(delta) < max(1.5, 4 * noise):
            raise RuntimeError(f'ToF 변화({delta:+.1f}mm)가 잡음({noise:.1f}mm)에 비해 작습니다. '
                               '측정 스텝을 늘리거나 레일 구동 여부를 확인하세요.')
        result = {
            'steps': steps, 'before_mm': before['mean_mm'], 'after_mm': after['mean_mm'],
            'back_mm': back['mean_mm'], 'delta_mm': round(delta, 2), 'noise_mm': noise,
            'rail_dir': 1 if delta * steps > 0 else -1,
            'steps_per_mm': round(abs(steps / delta), 3),
            'return_error_mm': round(back['mean_mm'] - before['mean_mm'], 2),
        }
        if apply:
            self.update({'rail_dir': result['rail_dir'], 'steps_per_mm': result['steps_per_mm']})
            self.apply(ip)
            result['applied'] = True
        return result

    def teach(self, ip, slot):
        """Store the ToF distance at the current rail position as center / plus / minus."""
        if slot == 'home':
            self._idle_state(ip)
            sample = self._sample(ip, 1.5)
            saved = self.update({'home_mm': round(sample['mean_mm'], 1)})
            return {'slot': slot, 'sample': sample, 'config': saved}
        if slot not in SLOTS:
            raise ValueError('저장 위치는 home, center, plus, minus 중 하나여야 합니다.')
        self._idle_state(ip)
        sample = self._sample(ip, 1.5)
        config = self.snapshot()
        config[f'{slot}_mm'] = round(sample['mean_mm'], 1)
        config['taught'][slot] = True
        if all(config['taught'].values()):
            if (config['plus_mm'] - config['center_mm']) * (config['minus_mm'] - config['center_mm']) >= 0:
                raise RuntimeError('+측과 -측은 중앙을 사이에 두고 반대쪽이어야 합니다. 저장 위치를 확인하세요.')
        saved = self.update({f'{slot}_mm': config[f'{slot}_mm'], 'taught': {slot: True}})
        return {'slot': slot, 'sample': sample, 'config': saved}

    def target_of(self, slot=None, mm=None):
        config = self.snapshot()
        if slot is not None:
            if slot not in SLOTS + ('home',):
                raise ValueError('이동 위치는 home, center, plus, minus 중 하나여야 합니다.')
            return config[f'{slot}_mm']
        target = _number(mm, '목표 거리', 1, 2000)
        if not config['rail_min_mm'] <= target <= config['rail_max_mm']:
            raise ValueError(f"목표 거리는 Z축 안전 한계 {config['rail_min_mm']:g}~{config['rail_max_mm']:g}mm 안이어야 합니다.")
        if all(config['taught'].values()):
            points = [config[f'{s}_mm'] for s in SLOTS] + [config['home_mm']]
            low, high = min(points) - SPAN_MARGIN_MM, max(points) + SPAN_MARGIN_MM
            if not low <= target <= high:
                raise ValueError(f'목표 거리는 티칭 범위 {low:g}~{high:g}mm 안이어야 합니다.')
        return target

    def goto(self, ip, target_mm, hold=True):
        state = self.status(ip)
        require_rail_firmware(state)
        if not state.get('pid'):  # a running PID may still be correcting: a new target is fine
            self._idle_state(ip)
        elif state.get('stopped') or not state.get('tof_ok'):
            raise RuntimeError('비상정지 상태이거나 ToF 측정값이 없습니다.')
        config = self.snapshot()
        return pid_move(lambda cmd: self.command(ip, cmd), lambda: self.status(ip), target_mm,
                        config, self.cancelled, self.clock, hold=hold)

    def test_drop(self, ip, category, ratios):
        """Drop check without camera/AI: rail to the material's bin, tilt, 2 s, tilt back, rail to center."""
        if category not in CATEGORIES:
            raise ValueError(f'재질은 {", ".join(CATEGORIES)} 중 하나여야 합니다.')
        gx, gy = (float(r) for r in ratios)
        if not (math.isfinite(gx) and math.isfinite(gy) and gx > 0 and gy > 0):
            raise ValueError('감속비는 양수여야 합니다.')
        config = self.snapshot()
        pid_rail = config['mode'] == 'pid'
        dest = destination(config, category)
        if pid_rail:
            require_slots_taught(config, {dest['rail']})
        state = self._idle_state(ip, need_tof=pid_rail)
        if state.get('stopped'):
            raise RuntimeError('비상정지 상태입니다. 초기화 후 다시 시도하세요.')
        if state.get('x') != 0 or state.get('y') != 0:
            raise RuntimeError('트레이 X/Y가 원점이 아닙니다. 선별 초기화(원점 복귀) 후 시험하세요.')
        rx, ry = tilt_steps(dest['roll'], (gx, gy))
        home_pid = tof_pid_ready(state)  # the origin is a ToF distance in both modes
        target_mm = config[f"{dest['rail']}_mm"]
        require_in_limits(config, target_mm, '배출 위치')
        # Open loop: steps from the origin to the bin's ToF target (no more fixed ±15 mm).
        rail_steps = 0 if pid_rail else open_loop_steps(config, target_mm, config['home_mm'], state['steps_per_mm'])
        pid = lambda target: pid_move(lambda cmd: self.command(ip, cmd), lambda: self.status(ip), target,
                                      config, self.cancelled, self.clock, hold=False)
        tilted = False
        try:
            if pid_rail or home_pid:
                for cmd in parameter_commands(config):
                    self.command(ip, cmd)
            if pid_rail:
                pid(config[f"{dest['rail']}_mm"])
            elif rail_steps:
                self.command(ip, f'mzr{rail_steps}')
                after = self._wait_idle(ip)
                if after.get('rail_limit_count', 0) != state.get('rail_limit_count', 0):
                    raise RuntimeError(f"Z축 안전 한계로 정지했습니다: {after.get('rail_limit') or '사유 없음'}")
            self._check()
            tilted = True
            self._tray_move(ip, rx, ry)
            self.cancelled.wait(2)
            self._check()
            self._tray_move(ip, -rx, -ry)
            tilted = False
            if home_pid:
                pid(config['home_mm'])  # back to the sequence origin (ToF), PID off
            elif rail_steps:
                self.command(ip, f'mzr{-rail_steps}')
                self._wait_idle(ip)
        except Exception:
            if tilted:
                try:
                    self.command(ip, 'x')  # never leave a tilted tray moving
                except Exception:
                    pass
            raise
        return {**dest, 'tilt_steps': [rx, ry], 'rail_steps': 0 if pid_rail else rail_steps,
                'rail_target_mm': config[f"{dest['rail']}_mm"] if pid_rail else None,
                'home_mm': config['home_mm'] if home_pid else None}

    def _tray_move(self, ip, dx, dy):
        before = self.status(ip)
        self.command(ip, f'b{dx},{dy}')
        after = self._wait_idle(ip)
        if after.get('boot_id') != before.get('boot_id') or (after.get('x'), after.get('y')) != (before['x'] + dx, before['y'] + dy):
            raise RuntimeError('트레이 전도 완료 위치가 목표 스텝과 다릅니다.')

    def sweep(self, ip, cycles=3):
        """Leveling / repeatability: center -> plus -> center -> minus -> center, repeated."""
        cycles = int(cycles)
        if not 1 <= cycles <= 10:
            raise ValueError('반복 횟수는 1~10이어야 합니다.')
        config = self.snapshot()
        require_taught(config)
        self._idle_state(ip)
        rows = []
        order = ['center'] + ['plus', 'center', 'minus', 'center'] * cycles
        for slot in order:
            self._check()
            row = pid_move(lambda cmd: self.command(ip, cmd), lambda: self.status(ip), config[f'{slot}_mm'],
                           config, self.cancelled, self.clock, hold=False)
            row['slot'] = slot
            rows.append(row)
            self.cancelled.wait(.3)
        summary = {}
        for slot in SLOTS:
            hits = [r for r in rows if r['slot'] == slot]
            errors = [r['error_mm'] for r in hits]
            zs = [r['z'] for r in hits if isinstance(r.get('z'), (int, float))]
            summary[slot] = {
                'target_mm': hits[0]['target_mm'],
                'max_abs_error_mm': round(max(abs(e) for e in errors), 1),
                'tof_spread_mm': round(max(r['tof_mm'] for r in hits) - min(r['tof_mm'] for r in hits), 1),
                'step_spread': (max(zs) - min(zs)) if zs else None,
                'mean_settle_s': round(sum(r['elapsed_s'] for r in hits) / len(hits), 2),
            }
        return {'cycles': cycles, 'rows': rows, 'summary': summary}
