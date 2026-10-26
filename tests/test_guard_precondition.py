import datetime as dt
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'gateway'))
from cut_link import require_root_guard, UpdateError


class GuardPreconditionTest(unittest.TestCase):
    def check(self, ack=True, seconds=600, age=40, live_age=1, remaining=550000):
        now = dt.datetime.now(dt.timezone.utc)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / 'summary.json').write_text(json.dumps({
                'all_roles_acknowledged': ack, 'seconds': seconds, 'failures': []}))
            (root / 'session.json').write_text(json.dumps({
                'started_utc': (now-dt.timedelta(seconds=age)).isoformat()}))
            (root / 'timeline.jsonl').write_text(json.dumps({
                'utc': (now-dt.timedelta(seconds=live_age)).isoformat(), 'kind': 'uart',
                'text': f'I4ACAP t_ms=100 board=5 event=root_guard remaining_ms={remaining}'})+'\n')
            require_root_guard(root, root, 300, now=now)

    def test_five_acks_and_fresh_live_guard_allow_cut(self):
        self.check()

    def test_partial_application_prevents_cut(self):
        with self.assertRaises(UpdateError): self.check(ack=False)

    def test_expiring_guard_prevents_cut(self):
        with self.assertRaises(UpdateError): self.check(age=400)

    def test_cleared_live_guard_prevents_cut(self):
        with self.assertRaises(UpdateError): self.check(remaining=0)

    def test_stale_telemetry_prevents_cut(self):
        with self.assertRaises(UpdateError): self.check(live_age=11)

    def test_future_record_prevents_cut(self):
        with self.assertRaises(UpdateError): self.check(age=-1)


if __name__ == '__main__':
    unittest.main()
