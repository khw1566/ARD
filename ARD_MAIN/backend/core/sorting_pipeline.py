"""Fresh-image sorting with acknowledged commands and MCU position checks."""
import json
import math
import socket
import threading
import time

from core.rail_control import (destination, open_loop_steps, parameter_commands, pid_move, require_rail_firmware,
                               require_slots_taught, tilt_steps, tof_pid_ready, validate as rail_defaults)


class FrameInbox:
    def __init__(self):
        self.condition = threading.Condition()
        self.revision = 0
        self.frame = None
        self.capture_id = None
        self.tagged = {}  # at most four recent captures; preview cannot overwrite a matching shot

    def clear(self):
        with self.condition:
            self.frame = None
            self.capture_id = None
            self.tagged.clear()
            self.revision += 1
            self.condition.notify_all()

    def publish(self, frame, capture_id=None):
        with self.condition:
            self.revision += 1
            self.frame = bytes(frame)
            self.capture_id = str(capture_id) if capture_id is not None else None
            if self.capture_id is not None:
                self.tagged[self.capture_id] = (self.revision, self.frame)
                while len(self.tagged) > 4:
                    self.tagged.pop(next(iter(self.tagged)))
            self.condition.notify_all()

    def capture(self, trigger, cancelled, timeout=25):
        with self.condition:
            revision = self.revision
        # Never hold the inbox lock across a network call: HTTP frame publication
        # must be able to finish even while the trigger is waiting for its ACK.
        deadline = time.monotonic() + timeout
        receipt = trigger()
        expected_id = receipt.get('capture_id') if isinstance(receipt, dict) else None
        with self.condition:
            def available():
                if expected_id is not None:
                    shot = self.tagged.get(str(expected_id))
                    return shot is not None and shot[0] > revision
                return self.revision != revision and self.frame is not None
            while not available():
                if cancelled.is_set():
                    raise RuntimeError('선별 작업이 중단되었습니다.')
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError('새 촬영 이미지 수신 시간이 초과되었습니다.')
                self.condition.wait(min(remaining, .2))
            return self.tagged[str(expected_id)][1] if expected_id is not None else self.frame


def request_line(ip, command, timeout=3):
    with socket.create_connection((ip, 5002), timeout=timeout) as connection:
        connection.settimeout(timeout)
        connection.sendall((command + '\n').encode())
        data = b''
        while b'\n' not in data:
            part = connection.recv(1024)
            if not part or len(data) > 4096:
                raise RuntimeError('DriveMCU 응답이 불완전합니다.')
            data += part
        return data.split(b'\n', 1)[0].decode().strip()


def validate_analysis(result):
    if not isinstance(result, dict) or result.get('error'):
        raise ValueError('이미지 분석에 실패했습니다.')
    name = result.get('object')
    category = result.get('category')
    if not isinstance(name, str) or not name.strip() or category not in ('Plastic', 'Metal', 'Glass', 'Paper', 'Others'):
        raise ValueError('AI 분석 응답의 물체명 또는 재질이 유효하지 않습니다.')
    return {'object': name.strip(), 'category': category}


class SortingPipeline:
    def __init__(self, inbox, trigger, analyze, publish, motor_update, transport=request_line, camera_reset=None):
        self.inbox, self.trigger, self.analyze = inbox, trigger, analyze
        self.publish, self.motor_update, self.transport = publish, motor_update, transport
        self.lock = threading.Lock()
        self.cancelled = threading.Event()
        self.active = False
        self.ip = None
        self.boot_id = None
        self.camera_reset = camera_reset
        self.resetting = False
        self.home_accepted = False
        self.home_timeout = 45  # s, step 1 tray return

    def reserve(self):
        with self.lock:
            if self.active or self.resetting:
                return False
            self.active = True
            self.cancelled.clear()
            return True

    def reserve_reset(self):
        with self.lock:
            if self.active or self.resetting:
                return False
            self.resetting = True
            self.active = True
            self.cancelled.clear()
            return True

    def reset(self, ip):
        """Explicit operator reset: clear capture state and return X/Y, never rename zero."""
        self.ip, self.boot_id = ip, None
        try:
            self.command('x')  # explicit reset first halts/clears any manual move queue
            self.inbox.clear()
            if self.camera_reset:
                self.camera_reset()
            self.check_cancelled()
            before = self.status()
            self.boot_id = before['boot_id']
            self.command('home')
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                state = self.status()
                if state['stopped']:
                    raise RuntimeError('초기화 중 비상정지가 감지되었습니다.')
                if not state['busy'] and not state['pid'] and state['x'] == 0 and state['y'] == 0:
                    self.motor_update('reset_xy')
                    self.publish(step='idle', phase='idle', is_running=False, step_num=0, progress=0,
                                 step_desc='촬영 상태 초기화·트레이 수평 복귀 완료. 다시 시작할 수 있습니다.',
                                 error=None, retry_ready=True)
                    return
                self.cancelled.wait(.1)
            raise RuntimeError('초기화 트레이 복귀 시간이 초과되었습니다.')
        except Exception as exc:
            try:
                self.transport(ip, 'x')
            except Exception:
                pass
            self.publish(step='error', phase='error', is_running=False, error=str(exc),
                         step_desc=f'초기화 실패: {exc}', retry_ready=False)
        finally:
            self.ip, self.boot_id = None, None
            with self.lock:
                self.resetting = False
                self.active = False

    def capture_frame(self):
        attempts = 2 if self.camera_reset else 1
        for attempt in range(attempts):
            try:
                return self.inbox.capture(self.trigger, self.cancelled)
            except Exception:
                self.check_cancelled()
                if attempt + 1 == attempts:
                    raise
                self.publish(step_desc='카메라 응답 지연: 촬영 상태 리셋 후 1회 재시도 중')
                self.camera_reset()
                self.inbox.clear()

    def check_cancelled(self):
        if self.cancelled.is_set():
            raise RuntimeError('선별 작업이 중단되었습니다.')

    def cancel(self):
        self.cancelled.set()
        if self.ip:
            self.transport(self.ip, 'x')

    def status(self):
        self.check_cancelled()
        response = self.transport(self.ip, 'status')
        try:
            state = json.loads(response)
            if state.get('protocol') != 1 or any(k not in state for k in ('boot_id', 'busy', 'stopped', 'pid', 'x', 'y', 'z', 'steps_per_mm')):
                raise ValueError()
        except (ValueError, TypeError):
            raise RuntimeError('DriveMCU 상태 조회 미지원: 업데이트된 펌웨어를 업로드하세요.') from None
        if self.boot_id is not None and state['boot_id'] != self.boot_id:
            raise RuntimeError('선별 작업 중 DriveMCU 재부팅이 감지되었습니다.')
        return state

    def move(self, command, offsets, timeout=60):
        self.check_cancelled()
        before = self.status()
        if before['busy'] or before['pid']:
            raise RuntimeError('DriveMCU가 동작 중이거나 레일 PID 모드입니다.')
        expected = {axis: before[axis] + offset for axis, offset in zip(('x', 'y', 'z'), offsets)}
        if self.transport(self.ip, command) != 'ACK:OK':
            raise RuntimeError('DriveMCU가 이동 명령을 수락하지 않았습니다.')
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = self.status()
            if current['boot_id'] != before['boot_id'] or current['stopped'] or current['pid']:
                raise RuntimeError('DriveMCU 재부팅·정지 또는 PID 전환이 감지되었습니다.')
            if current.get('rail_limit_count', 0) != before.get('rail_limit_count', 0):
                raise RuntimeError(f"Z축 안전 한계로 정지했습니다: {current.get('rail_limit') or '사유 없음'}")
            if current.get('tilt_limit_count', 0) != before.get('tilt_limit_count', 0):
                raise RuntimeError(f"디스크 기울기 안전 한계(±60°)로 정지했습니다: {current.get('tilt_limit') or '사유 없음'}")
            if not current['busy']:
                if any(current[axis] != expected[axis] for axis in expected):
                    raise RuntimeError('DriveMCU의 완료 위치가 목표 스텝과 다릅니다.')
                self.motor_update(command)
                return
            self.cancelled.wait(.1)
        raise RuntimeError('DriveMCU 이동 완료 확인 시간이 초과되었습니다.')

    def command(self, command):
        self.check_cancelled()
        response = self.transport(self.ip, command)
        if response != 'ACK:OK':
            raise RuntimeError(f'DriveMCU가 {command} 명령을 거부했습니다.')

    def home_xy(self):
        """Step 1: DriveMCU `home` (X/Y to 0, Z kept, PID off), confirmed by status."""
        response = self.transport(self.ip, 'home')
        if response != 'ACK:OK':
            raise RuntimeError(f'DriveMCU가 트레이 원점 복귀(home) 명령을 거부했습니다({response}).')
        self.home_accepted = True
        deadline = time.monotonic() + self.home_timeout
        while time.monotonic() < deadline:
            state = self.status()  # also stops on a reboot (boot_id)
            if state['stopped']:
                raise RuntimeError('트레이 원점 복귀 중 비상정지가 감지되었습니다.')
            if not state['busy'] and state['x'] == 0 and state['y'] == 0:
                self.motor_update('reset_xy')
                return
            self.cancelled.wait(.1)
        raise RuntimeError('트레이 X/Y 원점 복귀 시간이 초과되었습니다.')

    def rail_to(self, target_mm, rail):
        """ToF PID rail move; the PID is off again before the next tray move."""
        before = self.status()
        result = pid_move(self.command, self.status, target_mm, rail, self.cancelled)
        if self.status()['boot_id'] != before['boot_id']:
            raise RuntimeError('레일 이동 중 DriveMCU 재부팅이 감지되었습니다.')
        return result

    def run(self, config):
        moved = False
        tray_moved = False
        camera_phase = False
        self.ip = config['ip']
        self.boot_id = None
        self.home_accepted = False
        try:
            ratios = [float(config[f'gear_ratio_{axis}']) for axis in 'xy']  # tray tilt pair
            if any(not math.isfinite(v) or v <= 0 for v in ratios):
                raise ValueError('감속비는 양수여야 합니다.')
            state = self.status()
            self.boot_id = state['boot_id']
            # Step 1 (initialize): board checks, rail to the capture position by ToF PID,
            # tray X/Y to origin. Nothing has moved when a check fails.
            self.publish(step='initializing', step_num=1, progress=5, is_running=True, phase='init',
                         step_desc='원점 복귀: DriveMCU 상태 확인 중', detected_object='-', detected_material='-',
                         target_yaw=0, dump_roll=0, current_yaw=0, current_roll=0,
                         current_rail_z=0, target_rail_z=0, error=None)
            if state['busy'] or state['pid'] or state.get('jogging'):
                raise RuntimeError('분류 시작 전에 모터 정지 및 레일 PID 해제가 필요합니다.')
            if state['stopped']:
                raise RuntimeError('이전 정지 상태가 남아 있습니다. 초기화 / 재시도 준비 버튼을 누르세요.')
            # Origin (user spec): tray level (X/Y 0) and rail at home_mm by ToF PID, in both
            # modes. PID mode: bins are taught ToF targets. Open loop: bins are ±15 mm steps
            # from the origin; without ToF/PID the current rail position is the start (warned).
            rail = config.get('rail')
            rail_cfg = rail or rail_defaults({})
            pid_rail = bool(rail) and rail.get('mode') == 'pid'
            home_mm = rail_cfg['home_mm']
            start_rail = 0.
            factor = float(state['steps_per_mm'])
            if not math.isfinite(factor) or factor <= 0:
                raise RuntimeError('DriveMCU 레일 보정값이 유효하지 않습니다.')
            if pid_rail:
                require_rail_firmware(state)
                require_slots_taught(rail, {d['rail'] for d in rail['destinations'].values()})
                if not state.get('tof_ok'):
                    raise RuntimeError('ToF 측정값이 없어 레일 PID를 사용할 수 없습니다.')
            home_pid = bool(rail) and tof_pid_ready(state)
            if state['x'] != 0 or state['y'] != 0:
                self.publish(step_desc='원점 복귀: 디스크 수평(X/Y 원점) 맞추는 중')
                self.home_xy()
            if home_pid:
                for cmd in parameter_commands(rail_cfg):
                    self.command(cmd)
                moved = True
                self.publish(step_desc=f'원점 복귀: 레일 ToF {home_mm:g}mm로 이동 중 (PID)')
                self.rail_to(home_mm, rail_cfg)
                init_desc = f'원점 복귀 완료 (디스크 수평 · 레일 ToF {home_mm:g}mm)'
            else:
                init_desc = ('원점 복귀: 디스크 수평 완료. ToF/PID를 쓸 수 없어 레일은 현재 위치를 '
                             f'시작 위치로 사용합니다 (원점 ToF {home_mm:g}mm 미적용).')
            self.publish(progress=10, step_desc=init_desc)
            # phase names the motion in progress so remote screens can animate it.
            self.publish(step='capturing', step_num=2, progress=20, phase='capture',
                         step_desc='새 이미지 촬영 및 수신 대기', current_rail_z=start_rail)
            camera_phase = True
            frame = self.capture_frame()
            camera_phase = False
            self.check_cancelled()
            self.publish(step='inferring', step_num=3, progress=35, phase='analyze',
                         step_desc='수신한 새 이미지 AI 분석 중')
            result = validate_analysis(self.analyze(frame))
            self.check_cancelled()
            # Rail + forward/backward tilt only (no disk turn); the table lives in the rail config.
            dest = destination(rail or rail_defaults({}), result['category'])
            rail_mm, roll, bin_name = dest['rail_mm'], dest['roll'], dest['name']
            # Bin ToF target (taught/stored) in both modes; it must lie inside the Z safety limits.
            tof_target = rail_cfg[f"{dest['rail']}_mm"]
            self.publish(detected_object=result['object'], detected_material=result['category'],
                         target_yaw=0, target_rail_z=rail_mm, dump_roll=roll, rail_slot=dest['rail'],
                         tilt=dest['tilt'], rail_tof_target=tof_target)
            # Open loop: steps from the origin to the bin's ToF target, not a fixed ±15 mm.
            rail_steps = 0 if pid_rail else open_loop_steps(rail_cfg, tof_target, home_mm, factor)
            rx, ry = tilt_steps(roll, ratios)
            self.publish(step='rail_moving', step_num=4, progress=50, phase='rail_out',
                         step_desc=f'{bin_name} 배출 위치로 레일 이송 중')
            moved = True
            if pid_rail:
                self.rail_to(tof_target, rail)
            elif rail_steps:
                self.move(f'mzr{rail_steps}', (0, 0, rail_steps))
            tilt_name = '전진' if roll > 0 else '후진'
            self.publish(current_rail_z=rail_mm, step='dumping', step_num=5, progress=65, phase='roll_out',
                         step_desc=f'{tilt_name} {abs(roll):g}도 전도 배출 중')
            tray_moved = True
            self.move(f'b{rx},{ry}', (rx, ry, 0))
            self.publish(current_roll=roll, phase='settle', step_desc='전도 완료 · 물체 낙하 대기')
            if self.cancelled.wait(2):
                self.check_cancelled()
            self.publish(step='homing', step_num=6, progress=85, phase='roll_back', step_desc='트레이 수평 복귀 중')
            self.move(f'b{-rx},{-ry}', (-rx, -ry, 0))
            self.publish(current_roll=0, phase='rail_back',
                         step_desc=f'레일 원점 ToF {home_mm:g}mm 복귀 중' if home_pid else '레일 시작 위치 복귀 중')
            if home_pid:
                self.rail_to(home_mm, rail_cfg)
            elif rail_steps:
                self.move(f'mzr{-rail_steps}', (0, 0, -rail_steps))
            self.publish(current_rail_z=start_rail, step='complete', step_num=6, progress=100, phase='done',
                         is_running=False, step_desc=f'{bin_name} 분류 구동 및 원점 복귀 확인')
        except Exception as error:
            moved = moved or self.home_accepted
            if moved:
                try:
                    # A camera-only failure leaves X/Y level. Do not latch E-STOP
                    # and permanently block the next PID enable after a capture timeout.
                    self.transport(self.ip, 'mzm0' if camera_phase and not tray_moved and not self.cancelled.is_set() else 'x')
                except Exception:
                    pass
            recovery = ''
            recovery_ok = True
            if self.camera_reset and camera_phase and not tray_moved and not self.cancelled.is_set():
                try:
                    self.camera_reset()
                    self.inbox.clear()
                    recovery = ' 카메라 촬영 상태를 초기화했습니다.'
                except Exception as exc:
                    recovery_ok = False
                    recovery = f' 카메라 복구 확인 실패: {exc}. 수동 초기화를 사용하세요.'
            if camera_phase and recovery_ok and not self.cancelled.is_set():
                try:
                    state = self.status()
                    recovery_ok = not state['stopped'] and not state['busy'] and not state['pid'] and state['x'] == 0 and state['y'] == 0
                except Exception:
                    recovery_ok = False
                if not recovery_ok:
                    recovery += ' 모터 정지 상태를 확인하고 수동 초기화를 실행하세요.'
            self.publish(step='error', phase='error', is_running=False, step_desc=str(error) + recovery,
                         error=str(error), retry_ready=(camera_phase or not moved) and not self.cancelled.is_set() and recovery_ok)
        finally:
            self.ip = None
            self.boot_id = None
            with self.lock:
                self.active = False
