# Limitations and remaining work

## Known problems

- **Color:** compositing and FX remain in encoded sRGB, not linear light.
  Wide-gamut sources are clipped to sRGB even when the final output is P3.
  Unknown/live profiles require a declaration; unsupported video tags and HDR
  have no automatic normalization/tone mapping. ISF image inputs are not individually
  color-managed. Some decoder/readback paths lose
  precision. Output conversion clips SDR gamut and does not configure display
  ICC profiles or signal encoding; physical 10-bit output remains unverified.
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
| P2 | Extend color management to linear compositing, HDR and output signaling. | Reference patches agree through FX/blending and on physical receivers; verify gamut, alpha and 10-bit precision. |
| P2 | Sign and notarize macOS releases. | A downloaded release opens on a clean Mac without a Gatekeeper workaround. |
