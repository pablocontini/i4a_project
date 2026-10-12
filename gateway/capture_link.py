#!/usr/bin/env python3
"""Capture a radio UART without pulsing reset or transmitting commands."""
import argparse
from datetime import datetime
from pathlib import Path
import time
import sys

import serial


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('--seconds must be between 1 and 3600')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=115200, timeout=0.25)
    port.port = args.port
    port.dtr = False
    port.rts = False
    pending = bytearray()
    try:
        port.open()
        with args.output.open('a', encoding='utf-8') as log:
            header = f'# capture {datetime.now().astimezone().isoformat()} port={args.port}\n'
            log.write(header); print(header, end='', flush=True)
            end = time.monotonic() + args.seconds
            while time.monotonic() < end:
                pending.extend(port.read(port.in_waiting or 1))
                while b'\n' in pending or len(pending) > 4096:
                    if b'\n' in pending:
                        line, _, pending = pending.partition(b'\n')
                    else:
                        line, pending = pending, bytearray()
                    text = line.decode('utf-8', errors='replace').rstrip('\r')
                    if text:
                        entry = f'{datetime.now().astimezone().isoformat()} {text}\n'
                        log.write(entry); log.flush(); print(entry, end='', flush=True)
            if pending:
                log.write(pending.decode('utf-8', errors='replace'))
    except KeyboardInterrupt:
        print('\nCapture stopped; radio remains running.')
    finally:
        port.close()


if __name__ == '__main__':
    main()
