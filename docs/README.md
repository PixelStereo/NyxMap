# Engineering status and roadmap

Compositing, timelines/cues, audio/MIDI/OSC interactions, recall delays and mask
editing are implemented. Recall Undo restores meshes, mattes and animation
transport, canceling pending transitions. Older audio snapshots need recapture to
include input/measure/frequency controls. Integer images retain up to 16-bit
channels through layer and ISF uploads.

Pause/speed zero hold source/effect frames, feedback and live inputs. Escape also
cancels pending work; GO/Play/recall releases its hold, while operator Pause needs
explicit release. Manual controls and blackout remain available.

Source color tags appear in the Media Bin and layer details. Viewport **Output
Color** converts a declared rendered input space to **sRGB**, **linear Rec.709**,
**Display P3** or **Rec.709/Gamma 2.4**, with exposure in linear light. **Unchanged**
preserves existing values. Settings are saved, undoable and independent per output.

## Known limitations

- **Color:** source tags are metadata, not automatic normalization. Mixed sources
  still need a common interpretation before compositing. Output conversion clips
  out-of-gamut SDR values; Gamma 2.4 assumes ideal black. Displays/receivers must
  match the encoding: no automatic display ICC configuration or output-tag signaling.
  RGBA16 UNORM is the default; some readbacks/outputs and HAP BC6 CPU fallback
  remain 8-bit/clipped. No verified physical 10-bit output, managed linear working
  space or PQ/HLG tone mapping. Missing profiles remain Unknown.
- **Time:** running show/media clocks and shader TIME catch up after stalls;
  TIMEDELTA stays capped at 250 ms before speed scaling. Missed simulation frames
  are not replayed. Snapshot fades share show time; manual faders/blackout use wall
  time. Receivers keep receiving held pictures; input acquisition may continue.
- **Resources:** soft budgets exclude some FFmpeg/driver/GPU allocations. Existing
  decoders retain threads. Rendering holds a shared engine lock; interaction queues
  drop new events beyond 8192. Readback waits are per output.
- **Media/editing:** the FFT initially needs 85.3 ms at 48 kHz, updating every
  21.3 ms. macOS ProRes 4444/XQ uses software decoding to preserve alpha. Recall
  delays apply to Parameter addresses, not source/topology or creation/deletion.
- **Hardware/distribution:** A/V recovery, receivers, hot-unplug and endurance need
  physical validation. Slow media IO can delay shutdown. macOS packages are ad-hoc
  signed without notarization; Gatekeeper may block downloads.

## Roadmap

| Priority | Work | Acceptance |
|---|---|---|
| P1 | Measure callback, locks, memory and control floods on target hardware. | 18 × 1080p layers, 60 Hz, two outputs; zero control drops. Callback p99 ≤1 ms and ≤25% of its period, zero overruns; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate sustained shows and physical receivers. | Eight hours without crash/stuck transport/decoder errors; A/V offset <40 ms without growth. Twenty reconnects: recovery within 5 s or actionable error. Verify receiver frame IDs under load. |
| P2 | Normalize mixed source profiles into an explicit working space, with overrides for untagged media. | Tagged sRGB/P3/video reference patches agree before compositing; unknown tags stay explicit. Measure physical output before claiming 10-bit/HDR. |

## Validation

CI retains targeted render/widget tests, package smoke tests, ASan, TSan and fuzzing;
Valgrind runs nightly/manually. Exhaustive ISF rendering stays outside CTest/CI:
`pinol_tests --check-isf isf` remains manual.

```sh
Pinol --endurance-csv session.csv show.pinol
python3 scripts/analyze_timeline_endurance.py session.csv --fps 60 --check
```

`--check` rejects callback/lock violations, drops, decoder errors and incomplete or
inactive telemetry. Start a fresh process: histograms are lifetime values. Record
hardware, driver, workload and outputs separately; a PASS does not certify them.
CSV v2/JSONL correlate stalls with cues without proving causality. OSC metrics:
`/composition/timing`, `/composition/budget`.

Conversion references: [sRGB/P3](https://www.w3.org/TR/css-color-4/#color-conversion-code),
[Gamma 2.4](https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#TRANSFER_BT1886).
