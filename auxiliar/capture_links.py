#!/usr/bin/env python3
"""Capture up to four ESP UARTs, client pings and manual markers on one timeline."""
import argparse
import datetime as dt
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time
import uuid


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def pairs(values, limit=None, unique_values=False):
    result = {}
    for value in values:
        label, sep, target = value.partition("=")
        if not sep or not target or not re.fullmatch(r"[A-Za-z0-9_-]+", label):
            raise ValueError("Use LABEL=VALUE with an alphanumeric label")
        if label in result:
            raise ValueError("Repeated label: " + label)
        result[label] = target
    if limit and len(result) > limit:
        raise ValueError(f"Use at most {limit} ports")
    if unique_values and len({p.casefold() for p in result.values()}) != len(result):
        raise ValueError("Serial ports must be unique")
    return result


class Recorder:
    def __init__(self, output, ports):
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=False)
        (self.output / "markers").mkdir()
        (self.output / "port_requests").mkdir()
        (self.output / "stop_requests").mkdir()
        self.lock = threading.Lock()
        self.started = time.monotonic()
        self.sequence = 0
        self.events = (self.output / "timeline.jsonl").open("w", encoding="utf-8", buffering=1)
        self.timeline = (self.output / "timeline.log").open("w", encoding="utf-8", buffering=1)
        self.raw, self.logs, self.counts = {}, {}, {}
        try:
            for label in ports:
                self.add_port(label)
        except BaseException:
            self.close()
            raise

    def add_port(self, label):
        with self.lock:
            if label in self.raw:
                raise ValueError("Repeated UART label: " + label)
            raw = (self.output / f"{label}.raw").open("xb", buffering=0)
            try:
                log = (self.output / f"{label}.log").open("x", encoding="utf-8", buffering=1)
            except BaseException:
                raw.close()
                raise
            self.raw[label], self.logs[label] = raw, log
            self.counts[label] = {"bytes": 0, "lines": 0, "connections": 0}

    def event(self, kind, label="host", **fields):
        # Every producer records the receive time before contending for disk.
        record = {"utc": utc(), "elapsed_s": round(time.monotonic() - self.started, 6),
                  "kind": kind, "label": label, **fields}
        with self.lock:
            self.sequence += 1
            record["seq"] = self.sequence
            self.events.write(json.dumps(record, ensure_ascii=True) + "\n")
            text = fields.get("text", json.dumps(fields, ensure_ascii=True))
            flat = text.replace("\r", "").replace("\n", " | ")
            self.timeline.write(f"{record['utc']} +{record['elapsed_s']:.6f} [{label}] {kind} {flat}\n")
            if label in self.logs:
                self.logs[label].write(f"{record['utc']} +{record['elapsed_s']:.6f} {kind} {text}\n")
                if kind == "uart":
                    self.counts[label]["lines"] += 1
                elif kind == "connected":
                    self.counts[label]["connections"] += 1
        if kind in ("connected", "disconnected", "marker", "fatal", "port_added", "port_rejected", "stop_requested"):
            print(f"{record['utc']} {kind} {label}: {fields}", flush=True)

    def bytes(self, label, data):
        # Only this label's UART thread writes its raw file.
        self.raw[label].write(data)
        self.counts[label]["bytes"] += len(data)

    def close(self):
        for stream in [*self.raw.values(), *self.logs.values(), self.events, self.timeline]:
            stream.close()


def serial_worker(label, port, recorder, stop, serial_factory, serial_errors, baud=115200):
    pending = b""
    last_error = None

    def flush_partial(reason):
        nonlocal pending
        if pending:
            recorder.event("partial", label, text=pending.decode("utf-8", errors="replace"), reason=reason)
            pending = b""

    while not stop.is_set():
        uart = None
        try:
            uart = serial_factory(port=None, baudrate=baud, timeout=0.2)
            uart.dtr = False
            uart.rts = False
            uart.port = port
            uart.open()
            last_error = None
            recorder.event("connected", label, port=port)
            while not stop.is_set():
                # Do not wait to fill a 4 KiB read: timestamp short log lines promptly.
                data = uart.read(min(max(uart.in_waiting, 1), 4096))
                if not data:
                    continue
                recorder.bytes(label, data)
                pending += data
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    recorder.event("uart", label, text=line.rstrip(b"\r").decode("utf-8", errors="replace"))
                if len(pending) > 16384:
                    flush_partial("line_limit")
        except serial_errors as exc:
            if str(exc) != last_error:
                recorder.event("disconnected", label, port=port, error=str(exc))
                last_error = str(exc)
            flush_partial("disconnect")
            stop.wait(1)
        finally:
            if uart is not None:
                try:
                    uart.close()
                except serial_errors as exc:
                    recorder.event("close_error", label, error=str(exc))
    flush_partial("stop")


def select_interface(name, configurations):
    normalize = lambda value: re.sub(r"[\s_-]", "", value).casefold()
    matches = [item for item in configurations if normalize(item["alias"]) == normalize(name)]
    if len(matches) != 1:
        raise ValueError("Interface not found or ambiguous: " + name)
    selected = matches[0]
    addresses = [str(ipaddress.IPv4Address(value)) for value in selected["addresses"]
                 if not ipaddress.IPv4Address(value).is_link_local
                 and not ipaddress.IPv4Address(value).is_unspecified]
    if selected["status"] != "Up" or len(addresses) != 1:
        raise ValueError("Connect interface to Grillo and obtain one IPv4 address first: " + selected["alias"])
    if selected.get("weak_host_send") != 0:
        raise ValueError("Interface must have WeakHostSend disabled for source-bound probes")
    return {**selected, "source_ip": addresses[0]}


def windows_interface(name):
    if os.name != "nt":
        raise ValueError("--interface currently supports Windows only")
    # Fixed script: the interface name is matched in Python, never interpolated into a shell.
    script = """$ErrorActionPreference='Stop'
Get-NetIPConfiguration | ForEach-Object {
  $ipif = Get-NetIPInterface -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4
  [pscustomobject]@{alias=$_.InterfaceAlias; index=$_.InterfaceIndex;
    status=[string]$_.NetAdapter.Status; addresses=@($_.IPv4Address.IPAddress);
    gateway=@($_.IPv4DefaultGateway.NextHop); weak_host_send=[int]$ipif.WeakHostSend}
} | ConvertTo-Json -Depth 4 -Compress
"""
    code, output = run_command(["powershell", "-NoProfile", "-NonInteractive", "-Command", script], timeout=20)
    if code:
        raise ValueError("Cannot inspect network interfaces: " + output.strip())
    data = json.loads(output.lstrip("\ufeff"))
    return select_interface(name, data if isinstance(data, list) else [data])


def ping_command(address, timeout_ms, windows=None, source=None):
    address = str(ipaddress.IPv4Address(address))
    windows = os.name == "nt" if windows is None else windows
    binding = [] if source is None else ["-S" if windows else "-I", str(ipaddress.IPv4Address(source))]
    if windows:
        return ["ping", "-4", *binding, "-n", "1", "-w", str(timeout_ms), address]
    return ["ping", "-4", *binding, "-n", "-c", "1", "-W", str(max(1, (timeout_ms + 999) // 1000)), address]


def ping_received(returncode, output):
    # Windows can exit 0 for "Destination host unreachable". Require an echo reply.
    return returncode == 0 and re.search(r"\bttl\s*=\s*\d+", output, re.IGNORECASE) is not None


def run_command(command, timeout=5):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout, creationflags=0x08000000 if os.name == "nt" else 0)
    return result.returncode, result.stdout.decode("utf-8", errors="replace")


def ping_worker(label, address, recorder, stop, interval=1.0, interface=None, timeout_ms=800):
    source = interface["source_ip"] if interface else None
    command = ping_command(address, timeout_ms, source=source)
    # Allow the system ping timeout to expire before the process watchdog.
    command_timeout = max(5, timeout_ms / 1000 + 2)
    while not stop.is_set():
        started = time.monotonic()
        try:
            code, output = run_command(command, timeout=command_timeout)
            recorder.event("ping", label, address=address, received=ping_received(code, output),
                           source_ip=source, interface=interface["alias"] if interface else None,
                           timeout_ms=timeout_ms,
                           returncode=code, duration_s=round(time.monotonic() - started, 4), text=output.strip())
        except (OSError, subprocess.TimeoutExpired) as exc:
            recorder.event("ping_error", label, address=address, source_ip=source,
                           interface=interface["alias"] if interface else None,
                           timeout_ms=timeout_ms, error=str(exc))
        stop.wait(max(0, interval - (time.monotonic() - started)))


def network_snapshot(recorder, reason):
    commands = [["ipconfig", "/all"], ["route", "print", "-4"], ["netsh", "wlan", "show", "interfaces"]]
    if os.name != "nt":
        commands = [["ip", "-4", "addr"], ["ip", "-4", "route"]]
    for command in commands:
        try:
            code, output = run_command(command)
            recorder.event("network", reason=reason, command=command, returncode=code, text=output)
        except (OSError, subprocess.TimeoutExpired) as exc:
            recorder.event("network_error", reason=reason, command=command, error=str(exc))


def write_marker(output, text):
    output = Path(output)
    if not (output / "session.json").is_file():
        raise ValueError("Capture session not found: " + str(output))
    name = output / "markers" / uuid.uuid4().hex
    temporary = name.with_suffix(".tmp")
    temporary.write_text(json.dumps({"marked_utc": utc(), "text": text}), encoding="utf-8")
    temporary.rename(name.with_suffix(".json"))


def read_markers(recorder, seen):
    changed = False
    for path in sorted((recorder.output / "markers").glob("*.json")):
        if path.name not in seen:
            data = json.loads(path.read_text(encoding="utf-8"))
            recorder.event("marker", **data)
            seen.add(path.name)
            changed = True
    return changed


def request_port(output, value):
    pairs([value], limit=4, unique_values=True)
    output = Path(output)
    if not (output / "session.json").is_file() or not (output / "port_requests").is_dir():
        raise ValueError("Start a new capture with this version before adding a port")
    if (output / "summary.json").exists():
        raise ValueError("Capture has already stopped")
    path = output / "port_requests" / uuid.uuid4().hex
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps({"port": value, "requested_utc": utc()}), encoding="utf-8")
    temporary.rename(path.with_suffix(".json"))


def read_port_requests(recorder, ports, seen, start_port):
    for path in sorted((recorder.output / "port_requests").glob("*.json")):
        if path.name in seen:
            continue
        seen.add(path.name)
        try:
            request = json.loads(path.read_text(encoding="utf-8"))
            addition = pairs([request["port"]])
            combined = pairs([f"{label}={port}" for label, port in ports.items()] +
                             [request["port"]], limit=4, unique_values=True)
        except (ValueError, KeyError, TypeError) as exc:
            recorder.event("port_rejected", request=path.name, error=str(exc))
            continue
        label, port = next(iter(addition.items()))
        recorder.add_port(label)
        ports.update(combined)
        session = recorder.output / "session.json"
        metadata = json.loads(session.read_text(encoding="utf-8"))
        metadata["ports"] = dict(ports)
        temporary = session.with_suffix(".tmp")
        temporary.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        temporary.replace(session)
        recorder.event("port_added", label, port=port, requested_utc=request["requested_utc"])
        start_port(label, port)


def request_stop(output):
    output = Path(output)
    if not (output / "session.json").is_file():
        raise ValueError("Capture session not found: " + str(output))
    if (output / "summary.json").exists():
        raise ValueError("Capture has already stopped")
    if not (output / "stop_requests").is_dir():
        raise ValueError("This capture does not support the stop command; use Ctrl+C in its terminal")
    path = output / "stop_requests" / uuid.uuid4().hex
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps({"requested_utc": utc()}), encoding="utf-8")
    temporary.rename(path.with_suffix(".json"))


def read_stop_requests(recorder):
    paths = sorted((recorder.output / "stop_requests").glob("*.json"))
    if not paths:
        return False
    request = json.loads(paths[0].read_text(encoding="utf-8"))
    recorder.event("stop_requested", requested_utc=request["requested_utc"])
    return True


def wait_for_stop(output, timeout=30):
    summary = Path(output) / "summary.json"
    deadline = time.monotonic() + timeout
    while True:
        if summary.is_file():
            return json.loads(summary.read_text(encoding="utf-8"))
        if time.monotonic() >= deadline:
            raise ValueError("Stop requested, but orderly shutdown was not confirmed. "
                             "Check the capture terminal; the process may have already exited "
                             "without a summary. No process was forcibly terminated.")
        time.sleep(min(0.1, max(0, deadline - time.monotonic())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    capture = sub.add_parser("start")
    capture.add_argument("--port", action="append", required=True, metavar="LABEL=COMx")
    capture.add_argument("--ping", action="append", default=[], metavar="LABEL=IPv4")
    capture.add_argument("--interface", help="Windows adapter name used as the fixed IPv4 ping source")
    capture.add_argument("--ping-timeout-ms", type=int, default=800,
                         help="Echo reply timeout in milliseconds (default: 800)")
    capture.add_argument("--output", type=Path, required=True)
    capture.add_argument("--baud", type=int, default=115200)
    capture.add_argument("--duration", type=float, default=0, help="Seconds, or 0 until Ctrl+C or stop")
    capture.add_argument("--notes", default="")
    mark = sub.add_parser("mark")
    mark.add_argument("--output", type=Path, required=True)
    mark.add_argument("text")
    add = sub.add_parser("add-port", help="Add a UART to a running capture (four total maximum)")
    add.add_argument("--output", type=Path, required=True)
    add.add_argument("--port", required=True, metavar="LABEL=COMx")
    finish = sub.add_parser("stop", help="Stop a running capture and wait for its summary")
    finish.add_argument("--output", type=Path, required=True)
    finish.add_argument("--timeout", type=float, default=30,
                        help="Seconds to wait for orderly shutdown (default: 30)")
    args = parser.parse_args()
    if args.action == "mark":
        try:
            write_marker(args.output, args.text)
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
        print("Marker saved")
        return 0
    if args.action == "add-port":
        try:
            request_port(args.output, args.port)
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
        print("Port requested; check port_added/port_rejected and connected in the capture terminal")
        return 0
    if args.action == "stop":
        try:
            if not 0 < args.timeout < float("inf"):
                raise ValueError("Stop timeout must be finite and greater than zero")
            request_stop(args.output)
            print("Stop requested; waiting for UARTs, probes and summary.json", flush=True)
            summary = wait_for_stop(args.output, args.timeout)
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
        print("Capture stopped; summary: " + str(args.output / "summary.json"))
        return int(bool(summary["failures"]))
    try:
        ports = pairs(args.port, limit=4, unique_values=True)
        probes = pairs(args.ping)
        interface = windows_interface(args.interface) if args.interface else None
        for label, address in probes.items():
            if address == "auto":
                if not interface or len(interface["gateway"]) != 1:
                    raise ValueError("An auto ping target requires --interface with one IPv4 gateway")
                probes[label] = interface["gateway"][0]
        for address in probes.values():
            ipaddress.IPv4Address(address)
        if args.baud <= 0 or not 0 <= args.duration < float("inf") or args.ping_timeout_ms <= 0:
            raise ValueError("Invalid baud, duration or ping timeout")
        import serial
    except ImportError:
        parser.error("Install pyserial: python -m pip install -r gateway/requirements-ota.txt")
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        parser.error(str(exc))
    try:
        recorder = Recorder(args.output, ports)
    except OSError as exc:
        parser.error(str(exc))
    stop = threading.Event()
    failures = []
    seen = set()
    seen_ports = set()
    metadata = {"started_utc": utc(), "ports": ports, "ping_targets": probes, "baud": args.baud,
                "ping_interface": interface, "ping_timeout_ms": args.ping_timeout_ms,
                "notes": args.notes, "host": sys.platform,
                "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "timestamps": "host receive time; not firmware event time", "serial_writes": False}
    (recorder.output / "session.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    def guarded(function, *arguments):
        try:
            function(*arguments)
        except Exception as exc:
            failures.append(repr(exc))
            stop.set()
            try:
                recorder.event("fatal", error=repr(exc))
            except OSError:
                print("Capture stopped after an I/O failure: " + repr(exc), file=sys.stderr)

    workers = [threading.Thread(target=guarded, args=(serial_worker, label, port, recorder, stop,
                serial.Serial, (OSError, serial.SerialException), args.baud)) for label, port in ports.items()]
    workers += [threading.Thread(target=guarded, args=(ping_worker, label, address, recorder, stop,
                                                     1.0, interface, args.ping_timeout_ms))
                for label, address in probes.items()]
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    print(f"Capture: {recorder.output.resolve()} (Ctrl+C to stop)", flush=True)
    if interface:
        print(f"Pings: {interface['alias']} source={interface['source_ip']} (fixed for this session)", flush=True)
    print("No UART commands are sent. Check connected events for all selected ports.", flush=True)
    for worker in workers:
        worker.start()

    def start_port(label, port):
        worker = threading.Thread(target=guarded, args=(serial_worker, label, port, recorder, stop,
                                  serial.Serial, (OSError, serial.SerialException), args.baud))
        workers.append(worker)
        worker.start()

    try:
        network_snapshot(recorder, "start")
        while not stop.wait(0.2):
            if read_stop_requests(recorder):
                stop.set()
                break
            read_port_requests(recorder, ports, seen_ports, start_port)
            if read_markers(recorder, seen):
                network_snapshot(recorder, "marker")
            if args.duration and time.monotonic() - recorder.started >= args.duration:
                stop.set()
    finally:
        stop.set()
        for worker in workers:
            worker.join()
        try:
            read_markers(recorder, seen)
            network_snapshot(recorder, "stop")
            recorder.event("stopped", counts=recorder.counts, failures=failures)
            temporary = recorder.output / "summary.tmp"
            temporary.write_text(json.dumps(
                {"stopped_utc": utc(), "counts": recorder.counts, "failures": failures}, indent=2), encoding="utf-8")
            temporary.replace(recorder.output / "summary.json")
        finally:
            recorder.close()
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
