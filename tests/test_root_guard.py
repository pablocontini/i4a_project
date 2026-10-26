import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'gateway'))
import root_guard
from flash_node import Packet, UpdateError


class RootGuardTest(unittest.TestCase):
    def run_cli(self, seconds=600, paused=False, different_hash=False, partial=False):
        data = bytearray(53)
        data[51] = 1
        data[52] = int(paused)
        statuses = {i: Packet(0x81, 31, i, 1, 1, data=bytes(data)) for i in range(5)}
        if different_hash:
            data[13] = 1
            statuses[0] = Packet(0x81, 31, 0, 1, 1, data=bytes(data))
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(sys, 'argv', ['root_guard.py', '--port', 'COM5', '--seconds', str(seconds),
                                      '--record-output', str(Path(tmp) / 'guard'), '--watch', '0']), \
             patch.object(root_guard, 'SerialTransport'), patch.object(root_guard, 'NodeUpdater') as updater, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            updater.return_value.status.return_value = statuses
            if partial:
                updater.return_value.command.side_effect = UpdateError('missing south')
            result = root_guard.main()
            return result, updater

    def test_guard_targets_all_boards_and_uses_bounded_seconds(self):
        result, updater = self.run_cli()
        self.assertEqual(result, 0)
        self.assertEqual(updater.call_args.args[1], 31)
        updater.return_value.command.assert_called_once_with(10, struct.pack('<I', 600))

    def test_clear_is_explicit(self):
        result, updater = self.run_cli(seconds=0)
        self.assertEqual(result, 0)
        updater.return_value.command.assert_called_once_with(10, b'\0' * 4)

    def test_paused_node_cannot_be_guarded(self):
        result, updater = self.run_cli(paused=True)
        self.assertEqual(result, 1)
        updater.return_value.command.assert_not_called()

    def test_mixed_firmware_cannot_be_guarded(self):
        result, updater = self.run_cli(different_hash=True)
        self.assertEqual(result, 1)
        updater.return_value.command.assert_not_called()

    def test_partial_ack_is_not_success(self):
        result, _ = self.run_cli(partial=True)
        self.assertEqual(result, 1)

    def test_invalid_duration_is_rejected_before_serial(self):
        with self.assertRaises(SystemExit):
            self.run_cli(seconds=901)


if __name__ == '__main__':
    unittest.main()
