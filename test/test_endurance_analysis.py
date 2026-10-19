"""Exercise the analyzer with the recorder's actual CSV schema and event names."""
import contextlib
import importlib.util
import io
import json
import csv
import subprocess
import sys
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


class RealtimeAcceptanceTest(unittest.TestCase):
    def rows(self):
        base = {"frames": "60", "audio_callback_count_lifetime": "100",
                "audio_callback_p99_ms_lifetime": "1", "audio_callback_over_quarter_lifetime": "1",
                "audio_callback_over_budget_lifetime": "0", "render_lock_wait_p99_ms_lifetime": "1",
                "render_lock_hold_p99_ms_lifetime": "8", "interaction_events_dropped_lifetime": "0",
                "ffmpeg_errors_total": "0", "late_events_lost": "0", "journal_lost": "0"}
        return [base, dict(base, audio_callback_count_lifetime="200")]

    def test_roadmap_threshold_boundaries(self):
        self.assertEqual(analysis.realtime_failures(self.rows()), [])
        for key in ("audio_callback_p99_ms_lifetime", "render_lock_wait_p99_ms_lifetime",
                    "render_lock_hold_p99_ms_lifetime", "interaction_events_dropped_lifetime",
                    "audio_callback_over_budget_lifetime", "ffmpeg_errors_total", "journal_lost"):
            rows = self.rows()
            rows[-1][key] = str(float(rows[-1][key]) + .001)
            self.assertTrue(analysis.realtime_failures(rows), key)
        rows = self.rows()
        rows[0]["audio_callback_over_quarter_lifetime"] = "2"
        self.assertTrue(analysis.realtime_failures(rows))

    def test_incomplete_or_invalid_data_never_passes(self):
        self.assertTrue(analysis.realtime_failures([]))
        for invalid in ("", "nan", "inf", "-1", "bad"):
            rows = self.rows()
            rows[-1]["render_lock_hold_p99_ms_lifetime"] = invalid
            self.assertTrue(analysis.realtime_failures(rows), invalid)
        rows = self.rows()
        del rows[-1]["audio_callback_over_budget_lifetime"]
        self.assertTrue(analysis.realtime_failures(rows))

    def test_cli_exit_status_and_legacy_telemetry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.csv"
            def write(rows):
                with path.open("w", newline="", encoding="utf-8") as stream:
                    stream.write('# {"schema":2}\n')
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
            rows = self.rows()
            write(rows)
            command = [sys.executable, str(Path(analysis.__file__)), str(path), "--fps", "60", "--check"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Realtime acceptance: PASS", result.stdout)
            rows[-1]["interaction_events_dropped_lifetime"] = "1"
            write(rows)
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("Realtime acceptance: FAIL", result.stdout)
            write([{"frames": "60"}])
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("missing/invalid", result.stdout)

    def test_stopped_audio_and_counter_reset_never_pass(self):
        rows = self.rows()
        rows[-1]["audio_callback_count_lifetime"] = "100"
        self.assertTrue(analysis.realtime_failures(rows))
        rows[-1]["audio_callback_count_lifetime"] = "50"
        self.assertTrue(analysis.realtime_failures(rows))
        for row in rows:
            row["frames"] = "0"
        self.assertTrue(analysis.realtime_failures(rows))


if __name__ == "__main__":
    unittest.main()
