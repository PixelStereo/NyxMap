# Workflow and performance ideas

Proposals from the code and competitor review, not implemented features. Performance gains require measurement.

## Priorities

| Priority | Proposal | Intended benefit |
|---|---|---|
| P1 | Show each parameter's active writer, competing drivers and pending recalls. Define override, hold and return-to-automation policies. | Predictable control. See [parameter control](parameter-control.md). |
| P1 | Expose frame-time percentiles, CPU/GPU costs by layer and FX, memory, output waits and events associated with stalls. | Identify the actual bottleneck rather than relying on average FPS. |
| P1 | Compute render dependencies from active outputs and previews; cache dependency order and index layer IDs. Preserve feedback and transport behavior. | Avoid processing unused branches and repeating structural searches every frame. |
| P1 | Bound readback and sharing waits with a frame-wide output budget. Let a slow output retain its last frame. | Prevent waits from accumulating across outputs. |
| P1 | Prepare expensive upcoming cues selectively: media opening, first frames, shaders and GPU resources. Show Preparing / Ready / Failed and bound memory use. | Reduce stalls at GO without preloading the whole project. |
| P2 | Preview snapshot/preset differences, included values and restored drivers before update or recall. Extend media usage to stored snapshots, presets and sequence references. | Safer edits, replacements and cleanup. |
| P2 | Add timeline editing across multiple keys/tracks, passage duplication, markers and MIDI/OSC gesture recording. | Faster show programming. |
| P2 | Expose named show controls driving several parameters, with explicit Replace / Add / Multiply modes and defined clamping. | Reusable performance controls without accidental writer conflicts. |
| P2 | Add an On Air workspace protecting sensitive edits and reducing preview/helper work. | Clearer operation and more rendering headroom. |
| P2 | Profile spectral-analysis peaks in the audio callback; evaluate bounded background FFT processing while keeping fast level detection. | Improve callback timing under load. |
| P3 | Separate the prepared editing state from the live Program state, with explicit transitions and isolated audio/external commands. | Rehearse or prepare the next scene without changing live outputs. Multiple monitors of the same state are not sufficient. |

Start with control ownership and profiling, then measured render/output fixes and cue preparation. Validate behavior and performance changes through GitHub CI and representative hardware measurements.

## Competitor references

- [SMODE](https://doc.smode.io/userGuide/tutorialsAndHow-Tos/playbackAndExport/use-optimisation-tools-to-accelerate-a-composition/): layer/FX profiling and ON-AIR optimization.
- [Modulo Kinetic](https://www.modulo-pi.com/media-servers/modulo-kinetic/) / [Modulo Player](https://www.modulo-pi.com/markets/live-performances/): preparation, playback and show operation.
- [Millumin](https://help.millumin.com/docs/playback/dashboard/): dashboard, timeline workflow and Silent Edition.
- [MadMapper](https://madmapper.com/madmapper/faq): complete scenes, selective cues and control combination.
- [QLab audition](https://qlab.app/docs/v5/tools/auditioning-cues/) / [loading](https://qlab.app/docs/v5/other-cues/transport-cues/): isolated rehearsal and selective preparation.
