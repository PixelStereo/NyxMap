# Limitations and remaining work

## Known problems

- **Color:** source profiles are displayed but not normalized before compositing.
  Mixed profiles can produce inconsistent colors. Output conversion assumes the
  declared input space, clips out-of-gamut SDR values and does not configure display
  ICC profiles or signal output encoding. HDR tone mapping and physical 10-bit
  output are unverified; some readbacks and decoder fallbacks lose precision.
- **Timing:** long stalls skip feedback/simulation steps. Physical A/V recovery and
  clock stability remain unmeasured. Input acquisition continues while paused.
- **Resources:** soft budgets miss some FFmpeg/driver/GPU allocations. Render-lock
  contention, retained decoder threads and control-event drops under load need
  measurement. Readback waits accumulate per output; slow IO can delay shutdown.
- **Media/editing:** FFT startup adds 85 ms at 48 kHz. ProRes 4444/XQ software
  decoding increases CPU load. Recall delays do not cover source/topology changes.
- **Distribution:** macOS packages lack Developer ID signing and notarization.

## Roadmap

| Priority | Work | Done when |
|---|---|---|
| P1 | Profile load and fix measured bottlenecks. | 18 × 1080p layers, 60 Hz, two outputs; zero control drops/overruns. Callback p99 ≤1 ms and ≤25% of its period; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate endurance, receivers and reconnection. | Eight hours without crash/stuck transport/decoder errors; A/V offset <40 ms without growth. Twenty reconnects recover within 5 s or report an actionable error. Receiver frame IDs remain correct under load. |
| P2 | Normalize mixed source profiles in an explicit working space; allow untagged-source overrides. | sRGB/P3/video reference patches agree before compositing. Verify output encoding and precision on physical hardware. |
| P2 | Sign and notarize macOS releases. | A downloaded release opens on a clean Mac without a Gatekeeper workaround. |
