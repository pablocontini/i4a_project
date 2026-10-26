#!/usr/bin/env python3
"""Temporarily refuse new direct Root admissions on all five boards of a home node."""
import argparse
import json
from pathlib import Path
import struct

from flash_node import NodeUpdater, SerialTransport, UpdateError, format_status
from cut_link import Recorder, RecordingPort, observe, utc

ROOT_GUARD = 10


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=int, required=True, help='0 clears; 1..900 seconds, RAM only')
    parser.add_argument('--record-output', type=Path, required=True, help='New evidence directory')
    parser.add_argument('--watch', type=float, default=2)
    args = parser.parse_args()
    if not 0 <= args.seconds <= 900 or not 0 <= args.watch <= 30:
        parser.error('seconds must be 0..900 and watch 0..30')
    recorder = Recorder(args.record_output, {'grillo_center': args.port})
    (args.record_output / 'session.json').write_text(json.dumps({
        'started_utc': utc(), 'port': args.port, 'seconds': args.seconds,
        'targets': 31, 'serial_writes': True, 'persistent': False,
        'effect': 'refuse new Root admissions; preserve active links and other peers'
    }, indent=2), encoding='utf-8')
    transport, failure = None, None

    def report(message):
        print(message, flush=True)
        recorder.event('control', 'grillo_center', text=message)

    try:
        transport = SerialTransport(args.port, 115200)
        transport.port = RecordingPort(transport.port, recorder)
        recorder.event('connected', 'grillo_center', port=args.port)
        updater = NodeUpdater(transport, 31, timeout=20, retries=2, trace=report)
        statuses = updater.status()
        if len(statuses) != 5 or any(len(p.data) != 53 or p.data[4] or p.data[49] or
                p.data[50] or not p.data[51] or p.data[52] for p in statuses.values()):
            raise UpdateError('All five boards must be ready, idle and outside recovery')
        if len({p.data[13:45] for p in statuses.values()}) != 1:
            raise UpdateError('All five boards must run the same firmware')
        for status in statuses.values():
            report(format_status(status))
        updater.command(ROOT_GUARD, struct.pack('<I', args.seconds))
        report(f'All five roles acknowledged Root guard: {args.seconds} seconds (0 clears).')
        observe(transport, args.watch)
        return 0
    except (OSError, ValueError, UpdateError, KeyboardInterrupt) as exc:
        failure = str(exc) or 'interrupted'
        report(f'Guard incomplete: {failure}. Some boards may have applied it; the RAM guard expires automatically.')
        return 1
    finally:
        if transport:
            transport.close()
        (args.record_output / 'summary.json').write_text(json.dumps({
            'stopped_utc': utc(), 'failures': [failure] if failure else [],
            'all_roles_acknowledged': failure is None,
            'seconds': args.seconds, 'counts': recorder.counts
        }, indent=2), encoding='utf-8')
        recorder.close()


if __name__ == '__main__':
    raise SystemExit(main())
