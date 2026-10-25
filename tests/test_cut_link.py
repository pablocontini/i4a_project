import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
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

    def test_recording_preserves_rx_bytes_lines_and_tx(self):
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'control'
            recorder = cut_link.Recorder(folder, {'grillo_center': 'COM7'})
            raw = Mock()
            raw.read.side_effect = [b'I4ACUT event=cut_', b'applied\r\npartial']
            port = cut_link.RecordingPort(raw, recorder)
            self.assertEqual(port.read(20), b'I4ACUT event=cut_')
            self.assertEqual(port.read(20), b'applied\r\npartial')
            port.write(b'command\n')
            port.close()
            recorder.close()
            self.assertEqual((folder / 'grillo_center.raw').read_bytes(), b'I4ACUT event=cut_applied\r\npartial')
            events = [cut_link.json.loads(line) for line in (folder / 'timeline.jsonl').read_text().splitlines()]
            self.assertEqual([e['kind'] for e in events], ['uart', 'uart_tx', 'partial'])
            self.assertEqual(events[0]['text'], 'I4ACUT event=cut_applied')
            raw.close.assert_called_once()

    def test_reference_requires_each_target_and_recent_recovery(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            events = [{'kind': 'ping', 'label': label, 'utc': '2026-10-22T14:00:00+00:00', 'received': True,
                       'interface': 'Wi-Fi 2', 'source_ip': '10.116.0.250'}
                      for label in ('gateway', 'rpi', 'internet') for _ in range(10)]
            timeline = folder / 'timeline.jsonl'
            timeline.write_text('\n'.join(cut_link.json.dumps(e) for e in events))
            now = cut_link.dt.datetime.fromisoformat('2026-10-22T14:00:01+00:00')
            cut_link.require_reference(folder, '2026-10-22T13:00:00+00:00', now)
            events.append({**events[-1], 'kind': 'ping_error', 'received': False})
            timeline.write_text('\n'.join(cut_link.json.dumps(e) for e in events))
            with self.assertRaises(cut_link.UpdateError):
                cut_link.require_reference(folder, '2026-10-22T13:00:00+00:00', now)
            events.pop()
            events[-1]['received'] = False
            timeline.write_text('\n'.join(cut_link.json.dumps(e) for e in events))
            with self.assertRaises(cut_link.UpdateError):
                cut_link.require_reference(folder, '2026-10-22T13:00:00+00:00', now)
            with self.assertRaises(cut_link.UpdateError):
                cut_link.require_reference(folder, '2026-10-22T15:00:00+00:00', now)
            events[-1]['received'] = True
            for fields, time in [({}, now + cut_link.dt.timedelta(seconds=11)),
                                 ({'source_ip': '169.254.1.2'}, now),
                                 ({'source_ip': '10.116.0.249'}, now),
                                 ({'interface': None}, now)]:
                changed = [*events[:-1], {**events[-1], **fields}]
                timeline.write_text('\n'.join(cut_link.json.dumps(e) for e in changed))
                with self.assertRaises(cut_link.UpdateError):
                    cut_link.require_reference(folder, '2026-10-22T13:00:00+00:00', time)


if __name__ == '__main__':
    unittest.main()
