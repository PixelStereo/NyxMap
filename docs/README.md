# Limitations and remaining work

## Known problems

- **Parameter control:** competing drivers can overwrite each other without a durable takeover. See [current rules and proposed improvements](parameter-control.md).
- **Color:** compositing and FX remain in encoded sRGB, not linear light.
  Unknown/live profiles require a declaration; some video tags are unsupported.
  ISF image inputs are not individually color-managed. Some decoder/readback
  paths lose precision. Output conversion does not configure display ICC
  profiles or signal encoding; physical 10-bit output remains unverified.
- **Timing:** long stalls skip feedback/simulation steps. Physical A/V recovery and
  clock stability remain unmeasured. Input acquisition continues while paused.
- **Resources:** soft budgets miss some FFmpeg/driver/GPU allocations. Render-lock
  contention, retained decoder threads and control-event drops under load need
  measurement. Readback waits accumulate per output; slow IO can delay shutdown.
- **Media/editing:** FFT startup adds 85 ms at 48 kHz. ProRes 4444/XQ software
  decoding increases CPU load. Recall delays do not cover source/topology changes.
- **Photoshop files:** a `.psd`/`.psb` layer shows all its layers or one chosen in
  its Source tab (RGB or grayscale, 8/16-bit). All layers use the file's merged
  image when present; otherwise visible layers are composited with normal blending
  only. Other blend modes, clipping, masks, layer effects, adjustment layers and
  smart objects are not reproduced.
- **Distribution:** macOS packages lack Developer ID signing and notarization.

## Roadmap

| Priority | Work | Done when |
|---|---|---|
| P1 | Profile load and fix measured bottlenecks. | 18 × 1080p layers, 60 Hz, two outputs; zero control drops/overruns. Callback p99 ≤1 ms and ≤25% of its period; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate endurance, receivers and reconnection. | Eight hours without crash/stuck transport/decoder errors; A/V offset <40 ms without growth. Twenty reconnects recover within 5 s or report an actionable error. Receiver frame IDs remain correct under load. |
| P2 | Evaluate optional linear-light compositing and verify output signaling. | Reference patches blend correctly, existing projects keep their appearance, and alpha handling and physical 10-bit precision are verified. |
| P2 | Sign and notarize macOS releases. | A downloaded release opens on a clean Mac without a Gatekeeper workaround. |

Additional proposals: [Workflow and performance ideas](ideas.md).
