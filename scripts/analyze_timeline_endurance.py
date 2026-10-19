#!/usr/bin/env python3
"""Report Pinol wall-clock pacing around cue/timeline actions.

Usage: python3 scripts/analyze_timeline_endurance.py session.csv --fps 60
The CSV comes from Pinol --endurance-csv, with adjacent .events.jsonl.
This is diagnostics, not an assertion of zero stutter at the physical receiver.
"""
import argparse
from bisect import bisect_left
import csv
import datetime as dt
import json
from pathlib import Path


def number(row, key):
    try:
        return float(row.get(key) or 0)
    except (TypeError, ValueError):
        return 0.0


def instant(value):
    if not value:
        return None
    try:
        value = dt.datetime.fromisoformat(str(value).replace("Z", "+00:00"))
        return value.replace(tzinfo=dt.timezone.utc) if value.tzinfo is None else value
    except ValueError:
        return None


def summarize(path, fps):
    metadata = {}
    with path.open(encoding="utf-8-sig", newline="") as f:
        def metrics():
            nonlocal metadata
            for line in f:
                if line.startswith("#"):
                    try:
                        value = json.loads(line[1:].strip())
                        if isinstance(value, dict):
                            metadata = value
                    except json.JSONDecodeError:
                        pass
                else:
                    yield line
        rows = list(csv.DictReader(metrics()))
    if not rows:
        raise ValueError("No 1Hz metrics rows found")
    budget_ms = 1000.0 / fps
    worst = max(rows, key=lambda r: number(r, "interval_max_ms"))
    slow = [r for r in rows if number(r, "interval_max_ms") > 1.5 * budget_ms]
    misses = sum(number(r, "missed") for r in rows)

    def increase(key):
        vals = [number(row, key) for row in rows]
        return sum(max(0, b - a) for a, b in zip(vals, vals[1:]))

    events_path = Path(str(path) + ".events.jsonl")
    cue_times, late_times = [], []
    late_count = 0
    if events_path.is_file():
        # Stream long recordings; retain only timestamps needed for correlation.
        with events_path.open(encoding="utf-8") as stream:
            for line in stream:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(event, dict):
                    continue
                kind = event.get("kind")
                if kind == "frame_over_2x_budget":
                    late_count += 1
                    t = instant(event.get("utc"))
                    if t is not None:
                        late_times.append(t.timestamp())
                elif kind in ("cue", "timeline", "snapshot", "snapshot_recall"):
                    t = instant(event.get("utc"))
                    if t is not None:
                        cue_times.append(t.timestamp())
    cue_times.sort()
    near = 0
    for t in late_times:
        first = bisect_left(cue_times, t - .25)
        if first < len(cue_times) and cue_times[first] <= t + .25:
            near += 1

    print(f"Session: {len(rows)} samples / {fps} Hz (budget {budget_ms:.3f} ms)")
    print(f"Worst frame interval: {number(worst, 'interval_max_ms'):.3f} ms at {worst.get('utc')}")
    print(f"Worst 1Hz p99 interval: {max(number(row, 'interval_p99_ms') for row in rows):.3f} ms")
    print(f"Seconds containing a >1.5x-budget frame: {len(slow)}/{len(rows)}")
    print(f"Missed deadlines (sum): {int(misses)}")
    print(f"Dropped output counter increase: {int(increase('outputs_dropped_total'))}")
    print(f"GPU sharing skip counter increase: {int(increase('shares_skipped_total'))}")
    print(f">2x-budget events: {late_count}, within 250 ms of a cue/timeline: {near}")
    if events_path.is_file():
        print(f"Correlated event log: {events_path}")
    else:
        print("No JSONL event file found: action correlation unavailable")
    if metadata.get("schema") != 2:
        print("Warning: only CSV schema 2 has verified GPU sharing skip counters; older files may silently log zero.")
    lost = sum(number(row, "late_events_lost") + number(row, "journal_lost") for row in rows)
    if lost:
        print(f"Warning: {int(lost)} telemetry events lost; action correlation is incomplete.")
    print("This report does not prove receiver-side smoothness: compare against an unchanged hardware baseline.")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("csv", type=Path)
    p.add_argument("--fps", type=int, choices=(25, 30, 50, 60, 120), required=True)
    args = p.parse_args()
    summarize(args.csv, args.fps)
