import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("capture_links", ROOT / "auxiliar/capture_links.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class QuickStop(threading.Event):
    def wait(self, timeout=None):
        return self.is_set()


class FakeSerial:
    def __init__(self, chunks, stop, stop_on_finish=True):
        self.chunks = list(chunks)
        self.stop = stop
        self.stop_on_finish = stop_on_finish
        self.dtr = self.rts = True
        self.port = None
        self.open_settings = None
        self.closed = False

    @property
    def in_waiting(self):
        return 4096

    def open(self):
        self.open_settings = (self.port, self.dtr, self.rts)

    def read(self, length):
        item = self.chunks.pop(0)
        if isinstance(item, Exception):
            raise item
        if not self.chunks and self.stop_on_finish:
            self.stop.set()
        return item

    def write(self, *_):
        raise AssertionError("A passive monitor must not transmit to the ESP")

    def close(self):
        self.closed = True


class CaptureTests(unittest.TestCase):
    def test_duplicate_ports_labels_and_fifth_port_are_rejected(self):
        for value in (["a=COM5", "b=com5"], ["a=COM5", "a=COM6"],
                      [f"p{i}=COM{i}" for i in range(5)], ["../bad=COM1"]):
            with self.subTest(value=value), self.assertRaises(ValueError):
                capture.pairs(value, limit=4, unique_values=True)

    def test_four_uart_workers_preserve_raw_bytes_and_one_timeline(self):
        with tempfile.TemporaryDirectory() as tmp:
            ports = {f"esp{i}": f"COM{i}" for i in range(4)}
            recorder = capture.Recorder(Path(tmp) / "session", ports)
            workers, devices = [], []
            payload = b"rst:0xc\r\npart" + b"ial\xff\n"
            for label in ports:
                stop = threading.Event()
                uart = FakeSerial([b"rst:0xc\r\npart", b"ial\xff\n"], stop)
                devices.append(uart)
                workers.append(threading.Thread(target=capture.serial_worker,
                    args=(label, ports[label], recorder, stop, lambda uart=uart, **_: uart, (OSError,))))
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join(timeout=2)
                self.assertFalse(worker.is_alive())
            recorder.close()
            records = [json.loads(line) for line in (recorder.output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual([r["seq"] for r in records], list(range(1, len(records) + 1)))
            for label in ports:
                self.assertEqual((recorder.output / f"{label}.raw").read_bytes(), payload)
                lines = [r["text"] for r in records if r["kind"] == "uart" and r["label"] == label]
                self.assertEqual(lines, ["rst:0xc", "partial\ufffd"])
                self.assertEqual(recorder.counts[label]["bytes"], len(payload))
            self.assertTrue(all(u.closed for u in devices))
            self.assertEqual([u.open_settings for u in devices], [(ports[label], False, False) for label in ports])

    def test_disconnect_does_not_join_two_different_boot_fragments(self):
        with tempfile.TemporaryDirectory() as tmp:
            recorder = capture.Recorder(Path(tmp) / "session", {"esp": "COM5"})
            stop = QuickStop()
            first = FakeSerial([b"old", OSError("unplugged")], stop, False)
            second = FakeSerial([b"new\n"], stop)
            devices = iter([first, second])
            capture.serial_worker("esp", "COM5", recorder, stop, lambda **_: next(devices), (OSError,))
            recorder.close()
            records = [json.loads(line) for line in (recorder.output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual([r["text"] for r in records if r["kind"] == "partial"], ["old"])
            self.assertEqual([r["text"] for r in records if r["kind"] == "uart"], ["new"])
            self.assertEqual((recorder.output / "esp.raw").read_bytes(), b"oldnew\n")
            self.assertEqual(recorder.counts["esp"]["connections"], 2)

    def test_incomplete_line_is_bounded_without_losing_raw(self):
        with tempfile.TemporaryDirectory() as tmp:
            recorder = capture.Recorder(Path(tmp) / "session", {"esp": "COM5"})
            stop = threading.Event()
            data = b"X" * 17000
            uart = FakeSerial([data], stop)
            capture.serial_worker("esp", "COM5", recorder, stop, lambda **_: uart, (OSError,))
            recorder.close()
            self.assertEqual((recorder.output / "esp.raw").read_bytes(), data)
            records = [json.loads(line) for line in (recorder.output / "timeline.jsonl").read_text().splitlines()]
            self.assertTrue(any(r.get("reason") == "line_limit" for r in records))

    def test_windows_unreachable_exit_zero_is_not_an_echo_reply(self):
        self.assertFalse(capture.ping_received(0, "Respuesta desde 10.0.0.1: Host de destino inaccesible."))
        self.assertTrue(capture.ping_received(0, "Respuesta desde 1.1.1.1: bytes=32 tiempo=10ms TTL=57"))
        self.assertFalse(capture.ping_received(1, "TTL=57"))
        self.assertTrue(capture.ping_received(0, "64 bytes from 1.1.1.1: icmp_seq=1 ttl=57 time=12 ms"))

    def test_probe_command_is_ipv4_numeric_and_has_no_shell(self):
        self.assertEqual(capture.ping_command("1.1.1.1", 800, True),
                         ["ping", "-4", "-n", "1", "-w", "800", "1.1.1.1"])
        with self.assertRaises(ValueError):
            capture.ping_command("1.1.1.1 & echo bad", 800)

    def test_long_probe_timeout_is_honored_and_recorded_for_replies_and_errors(self):
        interface = {"alias": "Wi-Fi 2", "source_ip": "10.116.0.2"}
        for error in (None, subprocess.TimeoutExpired("ping", 8.5)):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as tmp:
                recorder = capture.Recorder(Path(tmp) / "session", {})
                stop = threading.Event()

                def run(command, timeout):
                    self.assertGreater(timeout, 6.5)
                    self.assertEqual(command, capture.ping_command("1.1.1.1", 6500,
                                                                  source=interface["source_ip"]))
                    stop.set()
                    if error:
                        raise error
                    return 0, "Reply from 1.1.1.1: bytes=32 time=1500ms TTL=57"

                with mock.patch.object(capture, "run_command", side_effect=run):
                    capture.ping_worker("internet", "1.1.1.1", recorder, stop,
                                        interface=interface, timeout_ms=6500)
                recorder.close()
                records = [json.loads(line) for line in
                           (recorder.output / "timeline.jsonl").read_text().splitlines()]
                self.assertEqual(len(records), 1)
                self.assertEqual(records[0]["kind"], "ping_error" if error else "ping")
                self.assertEqual(records[0]["timeout_ms"], 6500)
                self.assertEqual(records[0]["source_ip"], "10.116.0.2")
                self.assertEqual(records[0]["interface"], "Wi-Fi 2")
                if not error:
                    self.assertTrue(records[0]["received"])

    def test_interface_binding_and_gateway_selection(self):
        config = {"alias": "Wi-Fi 2", "index": 6, "status": "Up",
                  "addresses": ["192.168.4.2"], "gateway": ["192.168.4.1"], "weak_host_send": 0}
        selected = capture.select_interface("WIFI-2", [config])
        self.assertEqual(selected["source_ip"], "192.168.4.2")
        self.assertEqual(capture.ping_command("1.1.1.1", 800, True, selected["source_ip"]),
                         ["ping", "-4", "-S", "192.168.4.2", "-n", "1", "-w", "800", "1.1.1.1"])
        for change in ({"status": "Disconnected"}, {"addresses": ["169.254.1.2"]},
                       {"weak_host_send": 1}, {"addresses": []},
                       {"addresses": ["192.168.4.2", "192.168.4.3"]}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                capture.select_interface("Wi-Fi 2", [{**config, **change}])
        with self.assertRaises(ValueError):
            capture.select_interface("Wi-Fi 2", [config, config])

    def test_markers_are_read_once_and_original_time_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            recorder = capture.Recorder(Path(tmp) / "session", {})
            (recorder.output / "session.json").write_text("{}")
            capture.write_marker(recorder.output, "RAMONA_ON")
            seen = set()
            self.assertTrue(capture.read_markers(recorder, seen))
            self.assertFalse(capture.read_markers(recorder, seen))
            recorder.close()
            records = [json.loads(line) for line in (recorder.output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["text"], "RAMONA_ON")
            self.assertIn("marked_utc", records[0])

    def test_existing_capture_directory_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(FileExistsError):
                capture.Recorder(tmp, {})

    def test_add_uart_preserves_existing_data_and_timeline(self):
        with tempfile.TemporaryDirectory() as tmp:
            ports = {"root": "COM5", "grillo": "COM7"}
            recorder = capture.Recorder(Path(tmp) / "session", ports)
            (recorder.output / "session.json").write_text(json.dumps({"ports": ports}))
            recorder.bytes("root", b"before\n")
            recorder.event("uart", "root", text="before")
            capture.request_port(recorder.output, "uplink=COM8")
            started = []

            def start(label, port):
                started.append((label, port))
                stop = threading.Event()
                uart = FakeSerial([b"linked\n"], stop)
                capture.serial_worker(label, port, recorder, stop, lambda **_: uart, (OSError,))
                self.assertEqual(uart.open_settings, ("COM8", False, False))

            seen = set()
            capture.read_port_requests(recorder, ports, seen, start)
            capture.read_port_requests(recorder, ports, seen, start)
            recorder.bytes("root", b"after\n")
            recorder.event("uart", "root", text="after")
            recorder.close()
            self.assertEqual(started, [("uplink", "COM8")])
            self.assertEqual((recorder.output / "root.raw").read_bytes(), b"before\nafter\n")
            self.assertEqual((recorder.output / "uplink.raw").read_bytes(), b"linked\n")
            self.assertEqual(json.loads((recorder.output / "session.json").read_text())["ports"], ports)
            records = [json.loads(s) for s in (recorder.output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual([r["seq"] for r in records], list(range(1, len(records) + 1)))

    def test_add_uart_rejects_duplicates_and_fifth_port_without_starting(self):
        with tempfile.TemporaryDirectory() as tmp:
            ports = {f"esp{i}": f"COM{i}" for i in range(4)}
            recorder = capture.Recorder(Path(tmp) / "session", ports)
            (recorder.output / "session.json").write_text(json.dumps({"ports": ports}))
            for value in ("extra=COM9", "esp0=COM9", "extra=com0"):
                capture.request_port(recorder.output, value)
            capture.read_port_requests(recorder, ports, set(), lambda *_: self.fail("Rejected port started"))
            recorder.close()
            self.assertEqual(len(ports), 4)
            records = [json.loads(s) for s in (recorder.output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual([r["kind"] for r in records], ["port_rejected"] * 3)
            (recorder.output / "summary.json").write_text("{}")
            with self.assertRaises(ValueError):
                capture.request_port(recorder.output, "extra=COM9")

    def test_stop_rejects_missing_unsupported_and_completed_sessions(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            with self.assertRaisesRegex(ValueError, "session not found"):
                capture.request_stop(output)
            (output / "session.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "does not support"):
                capture.request_stop(output)
            (output / "stop_requests").mkdir()
            (output / "summary.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "already stopped"):
                capture.request_stop(output)
            self.assertEqual(list((output / "stop_requests").iterdir()), [])

    def test_stale_session_stop_does_not_claim_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            recorder = capture.Recorder(Path(tmp) / "session", {})
            (recorder.output / "session.json").write_text("{}")
            recorder.close()  # Simulate an exited process that never wrote its summary.
            capture.request_stop(recorder.output)
            with self.assertRaisesRegex(ValueError, "shutdown was not confirmed"):
                capture.wait_for_stop(recorder.output, timeout=0.02)

    def test_file_stop_closes_uart_flushes_partial_line_and_writes_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "session"
            ready = threading.Event()
            devices = []
            result, failures = [], []

            class MonitoredSerial:
                def __init__(self, **_):
                    self.closed = False
                    self.sent = False
                    self.in_waiting = 1
                    devices.append(self)

                def open(self):
                    pass

                def read(self, _):
                    if not self.sent:
                        self.sent = True
                        ready.set()
                        return b"last partial line"
                    time.sleep(0.01)
                    return b""

                def close(self):
                    self.closed = True

            def run():
                try:
                    result.append(capture.main())
                except BaseException as exc:
                    failures.append(exc)

            argv = ["capture_links.py", "start", "--port", "grillo=COM17", "--output", str(output),
                    "--ping", "internet=1.1.1.1", "--ping-timeout-ms", "6500"]
            serial_module = types.SimpleNamespace(Serial=MonitoredSerial, SerialException=OSError)
            with mock.patch.object(sys, "argv", argv), mock.patch.dict(sys.modules, {"serial": serial_module}), \
                    mock.patch.object(capture.signal, "signal"), \
                    mock.patch.object(capture, "network_snapshot"), \
                    mock.patch.object(capture, "run_command", return_value=(0, "bytes=32 TTL=57")) as command:
                runner = threading.Thread(target=run, daemon=True)
                runner.start()
                try:
                    self.assertTrue(ready.wait(2), failures)
                    capture.request_stop(output)
                    summary = capture.wait_for_stop(output, timeout=3)
                finally:
                    if runner.is_alive() and (output / "session.json").exists() \
                            and not (output / "summary.json").exists():
                        capture.request_stop(output)
                    runner.join(3)
                self.assertFalse(runner.is_alive())
                self.assertEqual(failures, [])
                self.assertEqual(result, [0])
                self.assertTrue(all(device.closed for device in devices))
                self.assertEqual(summary["failures"], [])
                self.assertEqual(summary["counts"]["grillo"]["bytes"], len(b"last partial line"))
                self.assertTrue(command.called)
                self.assertEqual(command.call_args.kwargs["timeout"], 8.5)
            metadata = json.loads((output / "session.json").read_text())
            self.assertEqual(metadata["ping_timeout_ms"], 6500)
            records = [json.loads(line) for line in (output / "timeline.jsonl").read_text().splitlines()]
            self.assertEqual([r["text"] for r in records if r["kind"] == "partial"], ["last partial line"])
            self.assertEqual(len([r for r in records if r["kind"] == "stop_requested"]), 1)
            self.assertEqual(records[-1]["kind"], "stopped")
            self.assertFalse((output / "summary.tmp").exists())


if __name__ == "__main__":
    unittest.main()
