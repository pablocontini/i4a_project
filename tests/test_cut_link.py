import contextlib
import io
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'gateway'))
import cut_link
from flash_node import Packet


class CutLinkTest(unittest.TestCase):
    def run_cli(self, extra=(), paused=False):
        data = bytearray(53)
        data[51] = 1
        data[52] = int(paused)
        statuses = {i: Packet(0x81, 0x1f, i, 1, 1, data=bytes(data)) for i in range(5)}
        with patch.object(sys, 'argv', ['cut_link.py', '--port', 'COM5', '--role', 'south', *extra]), \
             patch.object(cut_link, 'SerialTransport') as transport, \
             patch.object(cut_link, 'NodeUpdater') as updater, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            updater.return_value.status.return_value = statuses
            result = cut_link.main()
            return result, updater.return_value, transport.return_value

    def test_one_radio_plus_center_and_duration(self):
        result, updater, transport = self.run_cli(['--seconds', '120'])
        self.assertEqual(result, 0)
        self.assertEqual(updater.roles, 0x12)
        updater.command.assert_called_once_with(9, struct.pack('<I', 120))
        transport.close.assert_called_once()

    def test_restore_sends_zero(self):
        result, updater, _ = self.run_cli(['--restore'])
        self.assertEqual(result, 0)
        updater.command.assert_called_once_with(9, struct.pack('<I', 0))

    def test_boot_paused_prevents_cut(self):
        result, updater, transport = self.run_cli(paused=True)
        self.assertEqual(result, 1)
        updater.command.assert_not_called()
        transport.close.assert_called_once()


if __name__ == '__main__':
    unittest.main()
