"""Bounded ImageSender command delivery; capture IDs fence late/replayed frames."""
import secrets
import socket
import threading
import time


def camera_line(ip, command, timeout=1.0):
    with socket.create_connection((ip, 5003), timeout=timeout) as connection:
        connection.settimeout(timeout)
        connection.sendall((command + '\n').encode('ascii'))
        data = b''
        while b'\n' not in data and len(data) < 1024:
            part = connection.recv(256)
            if not part:
                break
            data += part
        if b'\n' not in data:
            raise OSError('ImageSender response incomplete')
        return data.split(b'\n', 1)[0].decode('ascii', errors='replace').strip()


class CameraControl:
    def __init__(self, get_ip, queue_poll, transport=camera_line, clock=time.monotonic, pause=time.sleep):
        self.get_ip, self.queue_poll, self.transport = get_ip, queue_poll, transport
        self.clock, self.pause = clock, pause
        self.lock = threading.Lock()
        self.supports_ids = False

    def capture(self):
        with self.lock:
            capture_id = secrets.randbelow(0x7ffffffe) + 1
            ip = self.get_ip()
            tagged_attempt = bool(ip and ip not in ('Unknown', 'None'))
            if ip and ip not in ('Unknown', 'None'):
                for _ in range(2):  # same ID after a lost ACK, never a second logical capture
                    try:
                        response = self.transport(ip, f'capture {capture_id}')
                        if response == f'ACK:CAPTURE_START,{capture_id}':
                            self.supports_ids = True
                            return {'capture_id': str(capture_id), 'channel': 'tcp'}
                        if response.startswith('ERR:BUSY'):
                            raise RuntimeError('ImageSender가 이전 촬영/리셋을 처리 중입니다.')
                        if response == 'ERR:UNKNOWN':
                            tagged_attempt = False
                            self.supports_ids = False
                            # Explicitly identified old firmware; preserve its untagged protocol.
                            if self.transport(ip, 'c').startswith('ACK:CAPTURE_START'):
                                return {'channel': 'legacy_tcp'}
                            break
                    except OSError:
                        continue
            if self.supports_ids or tagged_attempt:
                self.queue_poll(f'capture {capture_id}')
                return {'capture_id': str(capture_id), 'channel': 'poll'}
            self.queue_poll('capture')
            return {'channel': 'legacy_poll'}

    def reset(self):
        """Reset camera driver, not Wi-Fi settings. Wait for owner-task completion."""
        with self.lock:
            ip = self.get_ip()
            if not ip or ip in ('Unknown', 'None'):
                self.queue_poll('reset_camera')
                raise RuntimeError('카메라 리셋을 예약했지만 IP가 없어 완료를 확인할 수 없습니다.')
            response = self.transport(ip, 'reset')
            if response != 'ACK:RESET_PENDING':
                raise RuntimeError(f'ImageSender 카메라 리셋 미지원/실패: {response}')
            deadline = self.clock() + 18
            while self.clock() < deadline:
                self.pause(.2)
                try:
                    reply = self.transport(ip, 'ping')
                except OSError:
                    continue
                if 'busy=0' in reply and 'reset=0' in reply and 'ready=1' in reply:
                    return True
            raise RuntimeError('ImageSender 카메라 리셋 완료 확인 시간이 초과되었습니다.')
