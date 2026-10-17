"""ARD image protocol v2 tests: lossy-channel simulation of the ESP32 sender.

SimSender mirrors ARD_ESP-IDF/ImageSenderMCU/main/img_tx.c (window, POLL,
selective repeat, CRC restart) so the PC receiver is exercised against the same
algorithm the firmware runs. Run: python -m unittest tests.test_image_protocol
"""
import os
import random
import socket
import sys
import threading
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
from network import image_protocol as p  # noqa: E402

WINDOW = 32
POLL_RETRIES = 8


class Channel:
    """Delivers datagrams with loss, duplication and reordering."""

    def __init__(self, rng, loss=0.0, dup=0.0, reorder=0.0, corrupt=None):
        self.rng, self.loss, self.dup, self.reorder, self.corrupt = rng, loss, dup, reorder, corrupt
        self.held = None

    def pass_through(self, packets):
        out = []
        for pkt in packets:
            if self.rng.random() < self.loss:
                continue
            if self.corrupt:
                pkt = self.corrupt(pkt)
            copies = 2 if self.rng.random() < self.dup else 1
            for _ in range(copies):
                if self.held is None and self.rng.random() < self.reorder:
                    self.held = pkt
                    continue
                out.append(pkt)
                if self.held is not None:
                    out.append(self.held)
                    self.held = None
        return out

    def flush(self):
        held, self.held = self.held, None
        return [held] if held is not None else []


class SimSender:
    def __init__(self, receiver, up, down, addr=("10.0.0.7", 40000)):
        self.rx, self.up, self.down, self.addr = receiver, up, down, addr
        self.frame_id = 100
        self.sent = 0

    def _deliver(self, packets):
        replies = []
        for pkt in self.up.pass_through(packets) + self.up.flush():
            replies += [r for r, _ in self.rx.handle(pkt, self.addr)]
        return self.down.pass_through(replies) + self.down.flush()

    def send(self, data, max_rounds=2000):
        fid = self.frame_id
        self.frame_id += 1
        count = (len(data) + p.CHUNK_SIZE - 1) // p.CHUNK_SIZE
        crc = zlib.crc32(data) & 0xFFFFFFFF
        received = bytearray((count + 7) // 8)
        next_new, seq, restarts = 0, 0, 1

        def chunk(i):
            self.sent += 1
            return p.build_data(fid, i, count, len(data), crc, data[i * p.CHUNK_SIZE:(i + 1) * p.CHUNK_SIZE])

        for _ in range(max_rounds):
            burst = []
            for i in range(next_new):
                if len(burst) >= WINDOW:
                    break
                if not received[i >> 3] & (1 << (i & 7)):
                    burst.append(chunk(i))
            while next_new < count and len(burst) < WINDOW:
                burst.append(chunk(next_new))
                next_new += 1
            pending = self._deliver(burst)
            seq += 1
            outcome = None
            for _attempt in range(POLL_RETRIES):
                pending += self._deliver([p.build_poll(fid, seq, count, len(data), crc)])
                for reply in pending:
                    _m, _v, _t, rfid, rcount, _r, state, _x, echo = p.STATUS.unpack_from(reply)
                    if rfid != fid:
                        continue
                    if state in (p.STATE_COMPLETE, p.STATE_CRC_ERROR, p.STATE_REJECTED):
                        outcome = state
                        break
                    if echo == seq and rcount == count:
                        received[:] = reply[p.STATUS.size:p.STATUS.size + len(received)]
                        outcome = p.STATE_INCOMPLETE
                        break
                pending = []
                if outcome is not None:
                    break
            if outcome is None:
                return False
            if outcome == p.STATE_COMPLETE:
                return True
            if outcome == p.STATE_REJECTED:
                return False
            if outcome == p.STATE_CRC_ERROR:
                if restarts == 0:
                    return False
                restarts -= 1
                received = bytearray(len(received))
                next_new = 0
        return False


def jpeg_like(rng, size):
    body = bytes(rng.getrandbits(8) for _ in range(size - 4))
    return b"\xff\xd8" + body + b"\xff\xd9"


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.frames = []
        self.logs = []
        self.rx = p.ImageReceiver(lambda f, a, i: self.frames.append(f), self.logs.append)

    def run_case(self, seed, size, **loss):
        rng = random.Random(seed)
        data = jpeg_like(rng, size)
        sender = SimSender(self.rx, Channel(rng, **loss), Channel(rng, **loss))
        ok = sender.send(data)
        return ok, data, sender

    def test_clean_channel(self):
        ok, data, sender = self.run_case(1, 60_000)
        self.assertTrue(ok)
        self.assertTrue(self.frames == [data], f"got {len(self.frames)} frames")
        self.assertEqual(sender.sent, (len(data) + p.CHUNK_SIZE - 1) // p.CHUNK_SIZE)

    def test_exact_chunk_boundaries_and_tiny_frames(self):
        for i, size in enumerate([1, 4, p.CHUNK_SIZE, p.CHUNK_SIZE + 1, 3 * p.CHUNK_SIZE]):
            self.frames.clear()
            rng = random.Random(i)
            data = bytes(rng.getrandbits(8) for _ in range(size))
            self.assertTrue(SimSender(self.rx, Channel(rng), Channel(rng)).send(data), size)
            self.assertTrue(self.frames == [data], f"got {len(self.frames)} frames")

    def test_heavy_loss_dup_reorder_many_frames(self):
        for seed in range(20):
            self.frames.clear()
            ok, data, sender = self.run_case(seed, 250_000, loss=0.15, dup=0.05, reorder=0.1)
            self.assertTrue(ok, f"seed {seed}")
            self.assertTrue(self.frames == [data], f"seed {seed}: exactly one intact frame, got {len(self.frames)}")

    def test_crc_mismatch_triggers_full_resend(self):
        rng = random.Random(5)
        data = jpeg_like(rng, 20_000)
        flipped = {"done": False}

        def corrupt_once(pkt):
            if not flipped["done"] and pkt[3] == p.TYPE_DATA and pkt[6] == 3:  # chunk 3
                flipped["done"] = True
                pkt = pkt[:-1] + bytes([pkt[-1] ^ 0xFF])
            return pkt

        sender = SimSender(self.rx, Channel(rng, corrupt=corrupt_once), Channel(rng))
        self.assertTrue(sender.send(data))
        self.assertTrue(self.frames == [data], f"got {len(self.frames)} frames")
        self.assertEqual(self.rx.stats["crc_errors"], 1)

    def test_back_to_back_frames_and_late_duplicates(self):
        rng = random.Random(9)
        sender = SimSender(self.rx, Channel(rng, loss=0.1, dup=0.2), Channel(rng, loss=0.1))
        sent = [jpeg_like(rng, 30_000 + k * 1000) for k in range(10)]
        for frame in sent:
            self.assertTrue(sender.send(frame))
        self.assertTrue(self.frames == sent)

    def test_rejects_inconsistent_header(self):
        bad = p.HEADER.pack(p.MAGIC, p.VERSION, p.TYPE_DATA, 1, 0, 5, 10, 100, 0) + b"x" * 10
        replies = self.rx.handle(bad, ("1.2.3.4", 1))
        self.assertEqual(p.STATUS.unpack_from(replies[0][0])[6], p.STATE_REJECTED)
        self.assertEqual(self.frames, [])

    def test_legacy_drops_frame_with_missing_middle_chunk(self):
        rng = random.Random(3)
        data = jpeg_like(rng, 5000)
        chunks = [data[i:i + 1436] for i in range(0, len(data), 1436)]
        markers = [0] + [1] * (len(chunks) - 2) + [2]
        addr = ("10.0.0.8", 1)
        for k, (m, c) in enumerate(zip(markers, chunks)):
            if k == 1:
                continue  # lost on air, exactly what the old protocol could not detect
            self.rx.handle(bytes([m]) + c, addr)
        # Old receiver would hand over len(data)-1436 bytes; the end marker still matches,
        # so the JPEG-shape check alone cannot catch a missing middle chunk.
        self.assertEqual(len(self.frames), 1)
        self.assertNotEqual(self.frames[0], data)


class SocketTest(unittest.TestCase):
    """End-to-end over real UDP sockets on localhost with the blocking serve() loop."""

    def test_real_socket_roundtrip(self):
        frames = []
        got = threading.Event()
        rx = p.ImageReceiver(lambda f, a, i: (frames.append(f), got.set()), lambda m: None)
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        stop = threading.Event()
        threading.Thread(target=p.serve, args=(server, rx, stop), daemon=True).start()

        rng = random.Random(11)
        data = jpeg_like(rng, 100_000)
        count = (len(data) + p.CHUNK_SIZE - 1) // p.CHUNK_SIZE
        crc = zlib.crc32(data) & 0xFFFFFFFF
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.connect(server.getsockname())
        client.settimeout(1.0)
        for i in range(count):
            if i % 7 == 3:
                continue  # drop on purpose
            client.send(p.build_data(5, i, count, len(data), crc, data[i * p.CHUNK_SIZE:(i + 1) * p.CHUNK_SIZE]))
        client.send(p.build_poll(5, 1, count, len(data), crc))
        reply = client.recv(4096)
        bitmap = reply[p.STATUS.size:]
        missing = [i for i in range(count) if not bitmap[i >> 3] & (1 << (i & 7))]
        self.assertEqual(missing, [i for i in range(count) if i % 7 == 3])
        for i in missing:
            client.send(p.build_data(5, i, count, len(data), crc, data[i * p.CHUNK_SIZE:(i + 1) * p.CHUNK_SIZE]))
        done = p.STATUS.unpack_from(client.recv(4096))
        self.assertEqual(done[6], p.STATE_COMPLETE)
        self.assertTrue(got.wait(2))
        self.assertTrue(frames == [data])
        stop.set()
        client.close()
        server.close()


if __name__ == "__main__":
    unittest.main()
