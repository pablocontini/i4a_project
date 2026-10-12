#!/usr/bin/env python3
"""Temporarily cut one Station radio through the center USB, without an OTA transaction."""
import argparse
import struct
import sys

from flash_node import NodeUpdater, ROLES, SerialTransport, UpdateError

RADIO_CUT = 9


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--role', choices=('north', 'south', 'east', 'west'), required=True)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--seconds', type=int, default=120)
    action.add_argument('--restore', action='store_true')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    seconds = 0 if args.restore else args.seconds
    if not args.restore and not 1 <= seconds <= 300:
        parser.error('--seconds must be between 1 and 300')
    transport = None
    try:
        transport = SerialTransport(args.port, 115200, log=print if args.verbose else None)
        # Check the whole ring without changing OTA or maintenance state.
        updater = NodeUpdater(transport, 0x1f, timeout=20, retries=2,
                              trace=print if args.verbose else None)
        statuses = updater.status()
        if any(len(p.data) != 53 or not p.data[51] or p.data[52] or p.data[49] or
               p.data[50] or p.data[4] for p in statuses.values()):
            raise UpdateError('Every board must be ready, idle and outside boot recovery')
        if len({p.data[13:45] for p in statuses.values()}) != 1:
            raise UpdateError('All five boards must run the same firmware')
        updater.roles = (1 << ROLES[args.role]) | (1 << ROLES['center'])
        updater.command(RADIO_CUT, struct.pack('<I', seconds))
        print(f'Request queued: {args.role}, ' +
              ('restore now.' if not seconds else f'cut for {seconds} seconds with automatic restoration.'))
        print('Verify the link loss/restoration using telemetry and client pings. An ACK confirms queuing.')
        return 0
    except (UpdateError, OSError) as exc:
        print(f'Radio control failed: {exc}', file=sys.stderr)
        return 1
    finally:
        if transport:
            transport.close()


if __name__ == '__main__':
    raise SystemExit(main())
