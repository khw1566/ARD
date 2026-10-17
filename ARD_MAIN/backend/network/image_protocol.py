"""ARD image transfer protocol v2 - PC receiver (UDP 5001).

Byte layout must match ARD_ESP-IDF/ImageSenderMCU/components/ard_proto/include/ard_img_proto.h.

ESP32 sends DATA chunks (index + frame CRC32) in windows of up to 32, then a
POLL. The receiver answers every POLL with a STATUS carrying a bitmap of the
chunks it holds, so the ESP32 resends only what is missing (selective repeat).
A frame is handed to the application only when every chunk is present and the
CRC32 of the reassembled JPEG matches, so corrupted images never reach analysis.

Packets that do not start with b"AI" are treated as the old Arduino format
(1-byte 0/1/2 marker) so the existing firmware keeps working during migration;
those frames are only accepted if they look like a complete JPEG.
"""
import socket
import struct
import time
import zlib

MAGIC = b"AI"
VERSION = 2
TYPE_DATA = 1
TYPE_POLL = 2
TYPE_STATUS = 3

CHUNK_SIZE = 1400
MAX_CHUNKS = 2048
MAX_FRAME = CHUNK_SIZE * MAX_CHUNKS

STATE_INCOMPLETE = 0
STATE_COMPLETE = 1
STATE_CRC_ERROR = 2
STATE_REJECTED = 3
SEQ_UNSOLICITED = 0xFFFF

HEADER = struct.Struct("<2sBBHHHHII")    # magic ver type frame idx count payload total crc
STATUS = struct.Struct("<2sBBHHHBBH")    # magic ver type frame count received state rsv echo
assert HEADER.size == 20 and STATUS.size == 14

ASSEMBLY_TIMEOUT_S = 5.0
COMPLETED_MEMORY = 16       # recently finished frame ids remembered per sender
LEGACY_MAX_FRAME = 1024 * 1024


def build_status(frame_id, chunk_count, received_count, state, echo_seq, bitmap=b""):
    return STATUS.pack(MAGIC, VERSION, TYPE_STATUS, frame_id, chunk_count,
                       received_count, state, 0, echo_seq) + bytes(bitmap)


def build_data(frame_id, idx, count, total, crc, payload):
    return HEADER.pack(MAGIC, VERSION, TYPE_DATA, frame_id, idx, count, len(payload), total, crc) + payload


def build_poll(frame_id, seq, count, total, crc):
    return HEADER.pack(MAGIC, VERSION, TYPE_POLL, frame_id, seq, count, 0, total, crc)


class _Assembly:
    __slots__ = ("frame_id", "total", "count", "crc", "buf", "bitmap", "received", "updated", "started")

    def __init__(self, frame_id, total, count, crc, now):
        self.frame_id = frame_id
        self.total = total
        self.count = count
        self.crc = crc
        self.buf = bytearray(total)
        self.bitmap = bytearray((count + 7) // 8)
        self.received = 0
        self.updated = now
        self.started = now

    def matches(self, total, count, crc):
        return (self.total, self.count, self.crc) == (total, count, crc)

    def has(self, idx):
        return self.bitmap[idx >> 3] & (1 << (idx & 7))

    def put(self, idx, payload):
        if self.has(idx):
            return False
        offset = idx * CHUNK_SIZE
        self.buf[offset:offset + len(payload)] = payload
        self.bitmap[idx >> 3] |= 1 << (idx & 7)
        self.received += 1
        return True

    def reset(self):
        self.bitmap = bytearray(len(self.bitmap))
        self.received = 0


class _Sender:
    def __init__(self):
        self.assembly = None
        self.completed = []          # recent (frame_id, total, crc), oldest first
        self.legacy_buf = None
        self.legacy_updated = 0.0


class ImageReceiver:
    """Protocol state machine, independent of sockets so it can be unit tested.

    handle(data, addr) returns a list of (bytes, addr) replies to send.
    on_frame(jpeg_bytes, addr, info) is called once per verified frame.
    on_log(message) receives human readable diagnostics.
    """

    def __init__(self, on_frame, on_log=print, clock=time.monotonic):
        self.on_frame = on_frame
        self.on_log = on_log
        self.clock = clock
        self.senders = {}
        self.stats = {"frames_ok": 0, "crc_errors": 0, "duplicates": 0, "abandoned": 0,
                      "rejected": 0, "legacy_frames": 0, "legacy_dropped": 0}

    # ------------------------------------------------------------------ v2
    def handle(self, data, addr):
        sender = self.senders.setdefault(addr[0], _Sender())
        if len(data) >= HEADER.size and data[:2] == MAGIC:
            return self._handle_v2(sender, data, addr)
        self._handle_legacy(sender, data, addr)
        return []

    def _reject(self, frame_id, count, reason, addr):
        self.stats["rejected"] += 1
        self.on_log(f"[IMG] 잘못된 패킷 ({addr[0]}): {reason}")
        return [(build_status(frame_id, count, 0, STATE_REJECTED, SEQ_UNSOLICITED), addr)]

    def _handle_v2(self, sender, data, addr):
        magic, ver, ptype, frame_id, idx, count, plen, total, crc = HEADER.unpack_from(data)
        if ver != VERSION:
            return self._reject(frame_id, count, f"지원하지 않는 버전 {ver}", addr)
        if ptype not in (TYPE_DATA, TYPE_POLL):
            return []
        expected_count = (total + CHUNK_SIZE - 1) // CHUNK_SIZE
        if not 0 < total <= MAX_FRAME or count != expected_count:
            return self._reject(frame_id, count, f"크기 불일치 total={total} count={count}", addr)
        now = self.clock()

        # Keyed with size and CRC too, so a rebooted ESP32 reusing a frame id is not
        # mistaken for a duplicate of an old frame.
        if (frame_id, total, crc) in sender.completed:
            # Late duplicate or the ESP32 missed our COMPLETE: confirm again.
            if ptype == TYPE_DATA:
                self.stats["duplicates"] += 1
                return []
            return [(build_status(frame_id, count, count, STATE_COMPLETE, idx), addr)]

        asm = self._assembly_for(sender, frame_id, total, count, crc, now)

        if ptype == TYPE_POLL:
            asm.updated = now
            return [(build_status(frame_id, count, asm.received, STATE_INCOMPLETE, idx, asm.bitmap), addr)]

        # DATA
        if idx >= count or len(data) != HEADER.size + plen:
            return self._reject(frame_id, count, f"청크 {idx}/{count} 길이 {plen} 오류", addr)
        last_len = total - (count - 1) * CHUNK_SIZE
        if plen != (last_len if idx == count - 1 else CHUNK_SIZE):
            return self._reject(frame_id, count, f"청크 {idx} 길이 {plen} 오류", addr)
        asm.updated = now
        if not asm.put(idx, data[HEADER.size:]):
            self.stats["duplicates"] += 1
            return []
        if asm.received < count:
            return []
        return self._finish(sender, asm, addr)

    def _assembly_for(self, sender, frame_id, total, count, crc, now):
        asm = sender.assembly
        if asm and asm.frame_id == frame_id and asm.matches(total, count, crc):
            return asm
        if asm and asm.received:
            self.stats["abandoned"] += 1
            self.on_log(f"[IMG] 프레임 {asm.frame_id} 미완성 폐기 ({asm.received}/{asm.count} 청크) - 새 프레임 {frame_id} 시작")
        sender.assembly = _Assembly(frame_id, total, count, crc, now)
        return sender.assembly

    def _finish(self, sender, asm, addr):
        actual = zlib.crc32(asm.buf) & 0xFFFFFFFF
        if actual != asm.crc:
            self.stats["crc_errors"] += 1
            self.on_log(f"[IMG] 프레임 {asm.frame_id} CRC 불일치 (기대 {asm.crc:08x}, 실제 {actual:08x}) - 전체 재전송 요청")
            asm.reset()
            return [(build_status(asm.frame_id, asm.count, 0, STATE_CRC_ERROR, SEQ_UNSOLICITED), addr)]
        sender.assembly = None
        sender.completed.append((asm.frame_id, asm.total, asm.crc))
        del sender.completed[:-COMPLETED_MEMORY]
        self.stats["frames_ok"] += 1
        info = {"frame_id": asm.frame_id, "chunks": asm.count,
                "elapsed_ms": round((self.clock() - asm.started) * 1000), "protocol": 2}
        self.on_frame(bytes(asm.buf), addr, info)
        return [(build_status(asm.frame_id, asm.count, asm.count, STATE_COMPLETE, SEQ_UNSOLICITED), addr)]

    def expire(self):
        """Drop stale assemblies; call periodically (e.g. on socket timeout)."""
        now = self.clock()
        for ip, sender in self.senders.items():
            asm = sender.assembly
            if asm and now - asm.updated > ASSEMBLY_TIMEOUT_S:
                self.stats["abandoned"] += 1
                self.on_log(f"[IMG] 프레임 {asm.frame_id} 시간 초과 폐기 ({asm.received}/{asm.count} 청크, {ip})")
                sender.assembly = None
            if sender.legacy_buf is not None and now - sender.legacy_updated > ASSEMBLY_TIMEOUT_S:
                sender.legacy_buf = None

    # -------------------------------------------------------------- legacy
    def _handle_legacy(self, sender, data, addr):
        if not data:
            return
        marker, payload = data[0], data[1:]
        sender.legacy_updated = self.clock()
        if marker == 0:
            sender.legacy_buf = bytearray(payload)
        elif marker in (1, 2) and sender.legacy_buf is not None:
            sender.legacy_buf.extend(payload)
        else:
            return
        if len(sender.legacy_buf) > LEGACY_MAX_FRAME:
            sender.legacy_buf = None
            return
        if marker != 2:
            return
        frame, sender.legacy_buf = bytes(sender.legacy_buf), None
        # Old format has no length/CRC: accept only an intact-looking JPEG.
        if frame[:2] == b"\xff\xd8" and frame.rstrip(b"\x00")[-2:] == b"\xff\xd9":
            self.stats["legacy_frames"] += 1
            self.on_frame(frame, addr, {"protocol": 1})
        else:
            self.stats["legacy_dropped"] += 1
            self.on_log(f"[IMG] 구형 UDP 프레임 손상으로 폐기 ({len(frame)} bytes) - 펌웨어를 ESP-IDF 버전으로 교체하세요")


def serve(sock, receiver, stop_event=None):
    """Blocking receive loop for a bound UDP socket."""
    sock.settimeout(1.0)
    while stop_event is None or not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            receiver.expire()
            continue
        except OSError as e:
            if stop_event is not None and stop_event.is_set():
                return
            # Windows reports ICMP port-unreachable of an earlier sendto as WSAECONNRESET.
            if getattr(e, "winerror", None) == 10054:
                continue
            raise
        for reply, dest in receiver.handle(data, addr):
            try:
                sock.sendto(reply, dest)
            except OSError:
                pass
        receiver.expire()
