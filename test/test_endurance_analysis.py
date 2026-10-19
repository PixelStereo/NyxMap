"""Exercise the analyzer with the recorder's actual CSV schema and event names."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "endurance_analysis", Path(__file__).resolve().parents[1] / "scripts/analyze_timeline_endurance.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class EnduranceAnalysisTest(unittest.TestCase):
    def report(self, events, schema=2, loss=0):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.csv"
            path.write_text(
                '# PINOL endurance metrics\n# ' + json.dumps({"schema": schema}) + '\n'
                'utc,frames,missed,interval_p99_ms,interval_max_ms,outputs_dropped_total,shares_skipped_total,late_events_lost,journal_lost\n'
                f'2026-10-19T12:00:00Z,60,0,17,18,2,4,0,{loss}\n'
                '2026-10-19T12:00:01Z,58,2,35,40,5,11,0,0\n', encoding="utf-8")
            Path(str(path) + ".events.jsonl").write_text(
                '\n'.join(json.dumps(event) for event in events) + '\ninvalid JSON\nnull\n', encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                analysis.summarize(path, 60)
            return output.getvalue()

    def test_actual_snapshot_recall_event_and_counters(self):
        result = self.report([
            {"kind": "frame_over_2x_budget", "utc": "2026-10-19T12:00:01.200Z"},
            {"kind": "snapshot_recall", "utc": "2026-10-19T12:00:01Z"},
        ])
        self.assertIn("within 250 ms of a cue/timeline: 1", result)
        self.assertIn("Missed deadlines (sum): 2", result)
        self.assertIn("Dropped output counter increase: 3", result)
        self.assertIn("GPU sharing skip counter increase: 7", result)
        self.assertNotIn("Warning:", result)

    def test_correlation_boundaries_unsorted_events_and_timezones(self):
        result = self.report([
            {"kind": "timeline", "utc": "2026-10-19T14:00:02+02:00"},
            {"kind": "snapshot_recall", "utc": "2026-10-19T12:00:00"},
            {"kind": "frame_over_2x_budget", "utc": "2026-10-19T12:00:00.250Z"},
            {"kind": "frame_over_2x_budget", "utc": "2026-10-19T12:00:00.251Z"},
            {"kind": "frame_over_2x_budget", "utc": "2026-10-19T12:00:01.750Z"},
            {"kind": "frame_over_2x_budget", "utc": "invalid"},
        ])
        self.assertIn(">2x-budget events: 4, within 250 ms of a cue/timeline: 2", result)

    def test_legacy_csv_and_lost_events_are_visible(self):
        result = self.report([], schema=1, loss=9)
        self.assertIn("older files may silently log zero", result)
        self.assertIn("9 telemetry events lost", result)
        self.assertIn("correlation is incomplete", result)


if __name__ == "__main__":
    unittest.main()
