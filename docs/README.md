# Engineering status and roadmap

Compositing, timelines/cues, audio/MIDI/OSC interactions, recall delays and undoable
mask editing are implemented. Audio snapshots now save input, measure and frequency
bounds: recapture older snapshots to include these controls.

## Known limitations

- **Show control:** Escape stops transports/fades, but does not freeze all ISF
  passes, feedback buffers or external inputs. Shader dt is capped at 250 ms and
  show dt at 2 s; long stalls can desynchronize clocks. Output fades and show fades
  use different time domains.
- **Color:** RGBA16 UNORM is the internal default; ISF FLOAT targets use RGBA32F.
  Images, CPU fallback/readback and some outputs remain 8-bit; HAP BC6 CPU fallback
  clips HDR. No verified end-to-end 10-bit output, linear-light working space,
  ICC/gamut conversion or PQ/HLG tone mapping.
- **Resources:** budgets are soft and exclude some FFmpeg/driver/GPU allocations.
  Existing decoders retain their threads. Rendering holds a shared engine lock;
  interaction overflow drops new events beyond 8192. Readback waits are per output.
- **Audio/video:** the 4096-sample FFT needs 85.3 ms initially at 48 kHz, updating
  every 21.3 ms. macOS ProRes 4444/XQ uses software decoding to preserve alpha,
  increasing CPU demand.
- **Editing:** independent recall delays apply to Parameter addresses, not
  source/topology changes or creation/deletion. Preset concordance/undo coverage
  still needs strengthening for geometry, animation definitions and delays.
- **Hardware/distribution:** receiver integrity, hot-unplug recovery and endurance
  need physical validation; slow media IO can delay shutdown. macOS packages are
  ad-hoc signed without Developer ID/notarization; Gatekeeper may block downloads.

## Roadmap

| Priority | Work | Acceptance |
|---|---|---|
| P1 | Define ISF freeze/resume, long-stall catch-up and safety-blackout semantics. | Stop/resume tests with feedback/external inputs and 60/120 Hz cues; no cumulative A/V drift. |
| P1 | Profile callback, lock contention, memory and control floods before restructuring ownership/scheduling. | 18 × 1080p layers at 60 Hz, two outputs; zero control drops. Callback p99 ≤1 ms and ≤25% of its period, zero overruns; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate sustained shows and real receivers. | Eight hours without crash/stuck transport or unexpected decoder errors; A/V offset <40 ms without growth. Twenty reconnect cycles: recover within 5 s or show an actionable error. Verify receiver frame IDs under load. |
| P2 | Preserve 16-bit images, then add explicit color/output transforms. | Reference gradients survive import/compositing; measure compatible physical outputs before claiming 10-bit/HDR. |
| P2 | Complete preset/snapshot regressions; optimize measured bottlenecks. | Undo preserves geometry, animation definitions and delays; compare frame/control metrics before and after. |

## Validation and diagnostics

CI retains targeted engine/render/widget tests, package smoke tests, ASan, TSan and
fuzzing; Valgrind runs nightly/manually. Timing failures gate on the first attempt.
The exhaustive ISF library render is excluded from CTest/CI; manual use remains:
`pinol_tests --check-isf isf`.

Record build, hardware/driver, media hashes and outputs when comparing versions:

```sh
Pinol --endurance-csv session.csv show.pinol
python3 scripts/analyze_timeline_endurance.py session.csv --fps 60
```

CSV v2 reports sharing skips; analysis correlates late frames with cues and flags
lost events. Correlation does not prove causality. OSC `/composition/timing` and
`/composition/budget` expose metrics; `_lifetime` values survive timing resets.
Software-rendering/sanitizer CI does not establish hardware acceptance.
