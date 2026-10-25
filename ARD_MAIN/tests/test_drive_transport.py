"""Loopback TCP tests. Never contact an MCU or send hardware commands."""
from pathlib import Path
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from network.drive_transport import DriveCommandDispatcher


class Board:
    def __init__(self, handler=None):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(16)
        self.listener.settimeout(0.1)
        self.address = self.listener.getsockname()
        self.handler = handler or (lambda connection, command: connection.sendall(b"ACK:OK\r\n"))
        self.commands = []
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopped.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with connection:
                connection.settimeout(1)
                data = b""
                try:
                    while b"\n" not in data:
                        part = connection.recv(128)
                        if not part:
                            break
                        data += part
                    if b"\n" in data:
                        command = data.decode().strip()
                        self.commands.append(command)
                        self.handler(connection, command)
                except OSError:
                    pass

    def close(self):
        self.stopped.set()
        self.listener.close()
        self.thread.join(1)


class DriveTransportTests(unittest.TestCase):
    def setUp(self):
        self.resources = []

    def tearDown(self):
        for resource in reversed(self.resources):
            resource.close()

    def board(self, handler=None):
        board = Board(handler)
        self.resources.append(board)
        return board

    def dispatcher(self, **kwargs):
        dispatcher = DriveCommandDispatcher(connect_timeout=0.15, ack_timeout=0.3, **kwargs)
        self.resources.append(dispatcher)
        return dispatcher

    def done(self, result):
        self.assertTrue(result.done.wait(3), "command worker did not finish")
        return result

    def test_burst_keeps_order_and_reads_every_ack(self):
        board, sender = self.board(), self.dispatcher()
        commands = [f"mxr{i}" for i in range(20)]
        results = [sender.submit(*board.address, cmd) for cmd in commands]
        self.assertTrue(all(self.done(r).ok for r in results))
        self.assertEqual(board.commands, commands)

    def test_fragmented_ack(self):
        def handler(connection, command):
            connection.sendall(b"ACK:")
            time.sleep(0.01)
            connection.sendall(b"OK\r\n")
        board, sender = self.board(handler), self.dispatcher()
        self.assertTrue(self.done(sender.submit(*board.address, "mxr1")).ok)

    def test_rejection_is_reported(self):
        board = self.board(lambda c, cmd: c.sendall(b"ERR:REJECTED\r\n"))
        result = self.done(self.dispatcher().submit(*board.address, "mzm1"))
        self.assertEqual(result.status, "rejected")
        self.assertFalse(result.ok)
        self.assertEqual(board.commands, ["mzm1"])

    def test_lost_ack_never_replays_relative_move(self):
        board, sender = self.board(lambda c, cmd: None), self.dispatcher()
        result = self.done(sender.submit(*board.address, "mxr200"))
        self.assertEqual(result.status, "unconfirmed")
        self.assertEqual(board.commands, ["mxr200"])

    def test_timeout_never_replays_relative_move(self):
        board = self.board(lambda c, cmd: time.sleep(0.4))
        result = self.done(self.dispatcher().submit(*board.address, "myr200"))
        self.assertEqual(result.status, "unconfirmed")
        self.assertEqual(board.commands, ["myr200"])

    def test_only_connect_failures_are_retried(self):
        calls = []
        def connector(address, timeout):
            calls.append(address)
            if len(calls) < 3:
                raise ConnectionRefusedError("fixture")
            return socket.create_connection(address, timeout)
        board = self.board()
        result = self.done(self.dispatcher(connector=connector).submit(*board.address, "mxr1"))
        self.assertTrue(result.ok)
        self.assertEqual(len(calls), 3)
        self.assertEqual(board.commands, ["mxr1"])

    def test_stop_cancels_pending_moves(self):
        entered, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        def handler(connection, command):
            if command == "mxr1":
                entered.set()
                release.wait(1)
            connection.sendall(b"ACK:OK\n")
        board, sender = self.board(handler), self.dispatcher()
        first = sender.submit(*board.address, "mxr1")
        self.assertTrue(entered.wait(1))
        pending = [sender.submit(*board.address, f"mxr{i}") for i in (2, 3, 4)]
        stop = sender.submit(*board.address, "x")
        self.assertTrue(all(self.done(r).status == "cancelled" for r in pending))
        release.set()
        self.assertIn(self.done(first).status, ("accepted", "unconfirmed"))
        self.assertTrue(self.done(stop).ok)
        self.assertEqual(board.commands, ["mxr1", "x"])

    def test_stop_invalidates_unsent_connect_retry(self):
        attempted = threading.Event()
        calls = []
        def connector(address, timeout):
            calls.append(address)
            if len(calls) == 1:
                attempted.set()
                raise ConnectionRefusedError("fixture")
            return socket.create_connection(address, timeout)
        board, sender = self.board(), self.dispatcher(connector=connector)
        first = sender.submit(*board.address, "mxr100")
        self.assertTrue(attempted.wait(1))
        stop = sender.submit(*board.address, "x")
        self.assertEqual(self.done(first).status, "cancelled")
        self.assertTrue(self.done(stop).ok)
        self.assertEqual(board.commands, ["x"])

    def test_full_queue_reports_failure(self):
        entered, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        def handler(connection, command):
            entered.set()
            release.wait(1)
            connection.sendall(b"ACK:OK\n")
        board, sender = self.board(handler), self.dispatcher(max_pending=1)
        first = sender.submit(*board.address, "mxr1")
        self.assertTrue(entered.wait(1))
        second = sender.submit(*board.address, "mxr2")
        third = self.done(sender.submit(*board.address, "mxr3"))
        self.assertEqual(third.status, "busy")
        release.set()
        self.assertTrue(self.done(first).ok and self.done(second).ok)

    def test_expired_command_is_not_sent(self):
        entered, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        def handler(connection, command):
            entered.set()
            release.wait(1)
            connection.sendall(b"ACK:OK\n")
        board, sender = self.board(handler), self.dispatcher(max_age=0.01)
        first = sender.submit(*board.address, "mxr1")
        self.assertTrue(entered.wait(1))
        old = sender.submit(*board.address, "mxr2")
        time.sleep(0.03)
        release.set()
        self.assertTrue(self.done(first).ok)
        self.assertEqual(self.done(old).status, "expired")
        self.assertEqual(board.commands, ["mxr1"])

    def test_selection_guard_is_checked_before_network(self):
        def forbidden(*args, **kwargs):
            self.fail("inactive board opened a connection")
        result = self.done(self.dispatcher(connector=forbidden).submit(
            "127.0.0.1", 5002, "mxr1", allowed=lambda: False))
        self.assertEqual(result.status, "cancelled")

    def test_invalid_command_does_not_open_socket(self):
        sender = self.dispatcher(connector=lambda *a, **kw: self.fail("invalid command sent"))
        for command in ("", "mxr1\nx", "a" * 128, "한글"):
            self.assertEqual(self.done(sender.submit("127.0.0.1", 5002, command)).status, "invalid")


if __name__ == "__main__":
    unittest.main()
