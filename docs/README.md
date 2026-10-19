# Engineering work and validation

Start with the [19 October 2026 status audit](audit-status-2026-10-19.md): it maps
all 15 existing documents to current code, regression coverage, delivered fixes
and work still requiring hardware measurements. Historical audit findings are
not automatically current defects.

- Rendering and color: [color pipeline](COLOR_PIPELINE_AUDIT.md),
  [GPU sharing](gpu-share-validation.md), [macOS distribution](macos-release-signing.md).
- Show control: [cue latency](CUE_LATENCY.md),
  [timeline repeats](timeline-repeat-validation.md), [parameter delays](per-parameter-delay.md).
- Editing and audio: [audio interactions](audio-interactions.md),
  [masks and partial snapshot updates](mask-and-snapshot-editing.md),
  [UI responsiveness](ui-performance.md).
- Measurement: [performance work plan](performance-work-plan.md),
  [endurance telemetry](endurance-telemetry.md), [realtime acceptance](realtime-acceptance.md),
  [CI validation policy](ci-validation-policy.md).
- Historical evidence: [video/timing audit, 15 October](video-timing-audit-2026-10-15.md),
  [architecture/debug audit, 16 October](architecture-debug-audit-2026-10-16.md).
