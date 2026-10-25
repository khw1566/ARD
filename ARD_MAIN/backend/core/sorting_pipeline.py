"""Fresh-image sorting with acknowledged commands and MCU position checks."""
import json
import math
import socket
import threading
import time


class FrameInbox:
    def __init__(self):
        self.condition = threading.Condition()
        self.revision = 0
        self.frame = None

    def publish(self, frame):
        with self.condition:
            self.revision += 1
            self.frame = bytes(frame)
            self.condition.notify_all()

    def capture(self, trigger, cancelled, timeout=15):
        with self.condition:
            revision = self.revision
            trigger()
            deadline = time.monotonic() + timeout
            while self.revision == revision:
                if cancelled.is_set():
                    raise RuntimeError('선별 작업이 중단되었습니다.')
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError('새 촬영 이미지 수신 시간이 초과되었습니다.')
                self.condition.wait(min(remaining, .2))
            return self.frame


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


# Same numbered bins and clockwise bearings as the web model.
DESTINATIONS = {
    'Metal': (90., 15., '금속/캔'),
    'Glass': (90., -15., '유리/기타'),
    'Others': (90., -15., '유리/기타'),
    'Paper': (270., 15., '종이/팩'),
    'Plastic': (270., -15., '플라스틱'),
}


class SortingPipeline:
    def __init__(self, inbox, trigger, analyze, publish, motor_update, transport=request_line):
        self.inbox, self.trigger, self.analyze = inbox, trigger, analyze
        self.publish, self.motor_update, self.transport = publish, motor_update, transport
        self.lock = threading.Lock()
        self.cancelled = threading.Event()
        self.active = False
        self.ip = None
        self.boot_id = None

    def reserve(self):
        with self.lock:
            if self.active:
                return False
            self.active = True
            self.cancelled.clear()
            return True

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
            if not current['busy']:
                if any(current[axis] != expected[axis] for axis in expected):
                    raise RuntimeError('DriveMCU의 완료 위치가 목표 스텝과 다릅니다.')
                self.motor_update(command)
                return
            self.cancelled.wait(.1)
        raise RuntimeError('DriveMCU 이동 완료 확인 시간이 초과되었습니다.')

    def run(self, config):
        moved = False
        self.ip = config['ip']
        self.boot_id = None
        try:
            ratios = [float(config[f'gear_ratio_{axis}']) for axis in 'xy']
            if any(not math.isfinite(v) or v <= 0 for v in ratios):
                raise ValueError('감속비는 양수여야 합니다.')
            state = self.status()
            self.boot_id = state['boot_id']
            if state['busy'] or state['pid']:
                raise RuntimeError('분류 시작 전에 모터 정지 및 레일 PID 해제가 필요합니다.')
            if state['x'] != 0 or state['y'] != 0:
                raise RuntimeError('분류 시작 전에 트레이 X/Y 원점 복귀가 필요합니다.')
            # Open-loop rail: the physically aligned starting point is the center.
            # ToF readings do not participate in sorting or completion decisions.
            start_rail = 0.
            factor = float(state['steps_per_mm'])
            if not math.isfinite(factor) or factor <= 0:
                raise RuntimeError('DriveMCU 레일 보정값이 유효하지 않습니다.')
            # phase names the motion in progress so remote screens can animate it.
            self.publish(step='capturing', step_num=1, progress=10, is_running=True, phase='capture',
                         step_desc='새 이미지 촬영 및 수신 대기', detected_object='-', detected_material='-',
                         target_yaw=0, dump_roll=0, current_yaw=0, current_roll=0,
                         current_rail_z=start_rail, target_rail_z=0)
            frame = self.inbox.capture(self.trigger, self.cancelled)
            self.check_cancelled()
            self.publish(step='inferring', step_num=2, progress=30, phase='analyze',
                         step_desc='수신한 새 이미지 AI 분석 중')
            result = validate_analysis(self.analyze(frame))
            self.check_cancelled()
            yaw, rail_mm, bin_name = DESTINATIONS[result['category']]
            roll = 45.
            self.publish(detected_object=result['object'], detected_material=result['category'],
                         target_yaw=yaw, target_rail_z=rail_mm, dump_roll=roll)
            gx, gy = ratios
            rail = round((rail_mm - start_rail) * factor)
            sx, sy = round(yaw / 1.8 * gx), round(yaw / 1.8 * gy)
            rx, ry = round(roll / 1.8 * gx), -round(roll / 1.8 * gy)
            self.publish(step='rotating', step_num=3, progress=45, phase='rail_out',
                         step_desc=f'{bin_name} 배출 위치로 레일 이송 중')
            moved = True
            self.move(f'mzr{rail}', (0, 0, rail))
            self.publish(current_rail_z=rail_mm, phase='yaw_out', step_desc=f'{bin_name} 방향 {yaw:g}도 선회 중')
            self.move(f'b{sx},{sy}', (sx, sy, 0))
            self.publish(current_yaw=yaw, step='dumping', step_num=4, progress=70, phase='roll_out',
                         step_desc=f'{roll:g}도 전도 구동 중')
            self.move(f'b{rx},{ry}', (rx, ry, 0))
            self.publish(current_roll=roll, phase='settle', step_desc='전도 완료 · 물체 낙하 대기')
            if self.cancelled.wait(2):
                self.check_cancelled()
            self.publish(step='homing', step_num=5, progress=85, phase='roll_back', step_desc='트레이 수평 복귀 중')
            self.move(f'b{-rx},{-ry}', (-rx, -ry, 0))
            self.publish(current_roll=0, phase='yaw_back', step_desc='선회 원위치 복귀 중')
            self.move(f'b{-sx},{-sy}', (-sx, -sy, 0))
            self.publish(current_yaw=0, phase='rail_back', step_desc='레일 시작 위치 복귀 중')
            self.move(f'mzr{-rail}', (0, 0, -rail))
            self.publish(current_rail_z=start_rail, step='complete', step_num=5, progress=100, phase='done',
                         is_running=False, step_desc=f'{bin_name} 분류 구동 및 시작 위치 복귀 확인')
        except Exception as error:
            if moved:
                try:
                    self.transport(self.ip, 'x')
                except Exception:
                    pass
            self.publish(step='error', phase='error', is_running=False, step_desc=str(error), error=str(error))
        finally:
            self.ip = None
            self.boot_id = None
            with self.lock:
                self.active = False
