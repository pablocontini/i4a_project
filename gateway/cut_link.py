#!/usr/bin/env python3
"""Temporarily cut one Station radio through the center USB, without an OTA transaction."""
import argparse
import datetime as dt
import hashlib
import ipaddress
import json
import re
from pathlib import Path
import struct
import sys
import time

from flash_node import NodeUpdater, ROLES, SerialTransport, UpdateError, format_status

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'auxiliar'))
from capture_links import Recorder, utc, write_marker

RADIO_CUT = 9


class RecordingPort:
    """Preserve RX bytes and line timestamps while the controller owns this UART."""
    def __init__(self, port, recorder):
        self.port, self.recorder, self.pending = port, recorder, b''

    def __getattr__(self, name):
        return getattr(self.port, name)

    def read(self, size):
        data = self.port.read(size)
        if data:
            self.recorder.bytes('grillo_center', data)
            self.pending += data
            while b'\n' in self.pending:
                line, self.pending = self.pending.split(b'\n', 1)
                self.recorder.event('uart', 'grillo_center', text=line.rstrip(b'\r').decode('utf-8', errors='replace'))
            if len(self.pending) > 16384:
                self.recorder.event('partial', 'grillo_center', text=self.pending.decode('utf-8', errors='replace'))
                self.pending = b''
        return data

    def write(self, data):
        self.recorder.event('uart_tx', 'grillo_center', text=data.decode('ascii', errors='replace'))
        return self.port.write(data)

    def close(self):
        if self.pending:
            self.recorder.event('partial', 'grillo_center', text=self.pending.decode('utf-8', errors='replace'))
            self.pending = b''
        self.port.close()


def observe(transport, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        transport.receive(min(deadline, time.monotonic() + 1))


def require_reference(folder, since, now=None):
    now = now or dt.datetime.now(dt.timezone.utc)
    samples = {label: [] for label in ('gateway', 'rpi', 'internet')}
    for line in (Path(folder) / 'timeline.jsonl').read_text(encoding='utf-8').splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue  # The writer may still be appending the final line.
        if event.get('kind') in ('ping', 'ping_error') and event.get('utc', '') >= since and event.get('label') in samples:
            samples[event['label']].append(event)
    bindings = set()
    for label, probes in samples.items():
        if len(probes) < 10 or not all(p.get('received', False) for p in probes[-5:]):
            raise UpdateError(f'No valid reference for {label}: need >=10 probes and five final replies; no cut sent')
        age = (now - dt.datetime.fromisoformat(probes[-1]['utc'])).total_seconds()
        if not 0 <= age <= 10:
            raise UpdateError(f'Stale reference for {label}; no cut sent')
        for probe in probes:
            source = ipaddress.IPv4Address(probe.get('source_ip', '0.0.0.0'))
            if not probe.get('interface') or source.is_link_local or source.is_unspecified:
                raise UpdateError(f'Unbound reference for {label}; no cut sent')
            bindings.add((probe['interface'], str(source)))
    if len(bindings) != 1:
        raise UpdateError('Reference changed interface or source IPv4; no cut sent')


def require_root_guard(folder, control_folder, cut_seconds, now=None):
    """Require five application ACKs plus fresh live central guard telemetry."""
    now = now or dt.datetime.now(dt.timezone.utc)
    folder = Path(folder)
    summary = json.loads((folder / 'summary.json').read_text(encoding='utf-8-sig'))
    session = json.loads((folder / 'session.json').read_text(encoding='utf-8-sig'))
    started = dt.datetime.fromisoformat(session['started_utc'])
    duration = summary.get('seconds', 0)
    budget = duration - (now - started).total_seconds()
    if not summary.get('all_roles_acknowledged') or summary.get('failures') or \
            not 0 < duration <= 900 or not 0 <= (now-started).total_seconds() or budget < cut_seconds + 30:
        raise UpdateError('Root guard lacks five ACKs or sufficient remaining time; no cut sent')
    live = None
    for line in (Path(control_folder) / 'timeline.jsonl').read_text(encoding='utf-8').splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        match = re.search(r'board=5 event=root_guard remaining_ms=(\d+)', event.get('text', ''))
        if event.get('kind') == 'uart' and match:
            live = (event, int(match[1]))
    if not live or not 0 <= (now-dt.datetime.fromisoformat(live[0]['utc'])).total_seconds() <= 10 or \
            live[1] < (cut_seconds + 30) * 1000:
        raise UpdateError('Root guard has no fresh live countdown; no cut sent')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--role', choices=('north', 'south', 'east', 'west'), required=True)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--seconds', type=int, default=120)
    action.add_argument('--restore', action='store_true')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--record-output', type=Path, help='New directory for this control UART and command log')
    parser.add_argument('--marker-output', type=Path, help='Running capture_links session to mark phases')
    parser.add_argument('--baseline', type=float, default=0, help='Record this many seconds before sending the cut')
    parser.add_argument('--watch', type=float, default=0, help='Record this many seconds after the ACK (include cut and restoration)')
    parser.add_argument('--require-reference', action='store_true', help='Require healthy bound probes in --marker-output before cutting')
    parser.add_argument('--require-root-guard', type=Path, help='Successful root_guard.py record directory; require fresh live countdown')
    args = parser.parse_args()
    seconds = 0 if args.restore else args.seconds
    if not args.restore and not 1 <= seconds <= 300:
        parser.error('--seconds must be between 1 and 300')
    if not 0 <= args.baseline <= 600 or not 0 <= args.watch <= 1800:
        parser.error('--baseline must be 0..600 and --watch 0..1800 seconds')
    if args.require_reference and not args.marker_output:
        parser.error('--require-reference needs --marker-output')
    if args.require_root_guard and (not args.record_output or args.restore):
        parser.error('--require-root-guard needs --record-output and a timed cut')
    if args.require_root_guard and args.watch < seconds + 30:
        parser.error('--require-root-guard needs --watch >= seconds + 30 for cleanup and recovery')
    transport = None
    updater = None
    guard_cleared = False
    recorder = None
    failure = None

    def report(message):
        print(message, flush=True)
        if recorder:
            recorder.event('control', 'grillo_center', text=message)

    def mark(text):
        if recorder:
            recorder.event('marker', text=text, marked_utc=utc())
        if args.marker_output:
            write_marker(args.marker_output, text)

    def clear_guard():
        nonlocal guard_cleared
        updater.roles = 0x1f
        updater.command(10, struct.pack('<I', 0))
        guard_cleared = True
        mark('ROOT_GUARD_CLEAR_ACK')
        report('All five roles acknowledged clearing the Root admission guard.')

    try:
        if args.record_output:
            recorder = Recorder(args.record_output, {'grillo_center': args.port})
            (args.record_output / 'session.json').write_text(json.dumps({
                'started_utc': utc(), 'ports': {'grillo_center': args.port},
                'role': args.role, 'seconds': seconds, 'baseline_s': args.baseline,
                'watch_s': args.watch, 'serial_writes': True,
                'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'timestamps': 'host receive time; correlate independent captures by UTC'
            }, indent=2), encoding='utf-8')
        transport = SerialTransport(args.port, 115200, log=report if args.verbose and not recorder else None)
        if recorder:
            transport.port = RecordingPort(transport.port, recorder)
            recorder.event('connected', 'grillo_center', port=args.port)
        # Check the whole ring without changing OTA or maintenance state.
        updater = NodeUpdater(transport, 0x1f, timeout=20, retries=2,
                              trace=report if args.verbose else None)
        statuses = updater.status()
        if any(len(p.data) != 53 or not p.data[51] or p.data[52] or p.data[49] or
               p.data[50] or p.data[4] for p in statuses.values()):
            raise UpdateError('Every board must be ready, idle and outside boot recovery')
        if len({p.data[13:45] for p in statuses.values()}) != 1:
            raise UpdateError('All five boards must run the same firmware')
        for status in statuses.values():
            report(format_status(status))
        baseline_since = utc()
        mark('BASELINE_START')
        observe(transport, args.baseline)
        if args.require_reference:
            require_reference(args.marker_output, baseline_since)
        if args.require_root_guard:
            require_root_guard(args.require_root_guard, args.record_output, seconds)
        updater.roles = (1 << ROLES[args.role]) | (1 << ROLES['center'])
        mark('RESTORE_REQUEST' if not seconds else 'CUT_REQUEST')
        updater.command(RADIO_CUT, struct.pack('<I', seconds))
        report(f'Request queued: {args.role}, ' +
              ('restore now.' if not seconds else f'cut for {seconds} seconds with automatic restoration.'))
        mark('RESTORE_ACK' if not seconds else 'CUT_ACK')
        report('Verify the link loss/restoration using telemetry and client pings. An ACK confirms queuing.')
        if args.require_root_guard:
            # Restore admission after the bounded radio cut, then observe reconnection.
            observe(transport, seconds + 15)
            clear_guard()
            observe(transport, args.watch - seconds - 15)
        else:
            observe(transport, args.watch)
        if args.watch:
            mark('OBSERVATION_END')
            updater.roles = 0x1f
            for status in updater.status().values():
                report(format_status(status))
        return 0
    except (UpdateError, OSError, ValueError, KeyboardInterrupt) as exc:
        failure = str(exc) or 'interrupted'
        report(f'Radio control failed: {failure}')
        return 1
    finally:
        if args.require_root_guard and updater and transport and not guard_cleared:
            try:
                clear_guard()
            except (UpdateError, OSError, ValueError, KeyboardInterrupt) as exc:
                failure = failure or f'Root guard cleanup incomplete: {exc}'
                report('Root guard cleanup incomplete; its RAM deadline remains the fallback.')
        if transport:
            transport.close()
        if recorder:
            (args.record_output / 'summary.json').write_text(json.dumps({
                'stopped_utc': utc(), 'counts': recorder.counts,
                'failures': [failure] if failure else [],
                'root_guard_controlled': bool(args.require_root_guard),
                'root_guard_cleared': guard_cleared if args.require_root_guard else None
            }, indent=2), encoding='utf-8')
            recorder.close()


if __name__ == '__main__':
    raise SystemExit(main())
