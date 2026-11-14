# Roadmap

What is planned or proposed, **not available yet**. For what NyxMap does today, see the user guide; for known problems, see [Limitations](limitations.md). Performance gains require measurement, and every change is validated through GitHub CI.

## Release goals

| Priority | Work | Done when |
|---|---|---|
| P1 | Profile load and fix measured bottlenecks. | 18 × 1080p layers, 60 Hz, two outputs; zero control drops/overruns. Callback p99 ≤1 ms and ≤25% of its period; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate endurance, receivers and reconnection. | Eight hours without crash/stuck transport/decoder errors; A/V offset <40 ms without growth. Twenty reconnects recover within 5 s or report an actionable error. Receiver frame IDs remain correct under load. |
| P2 | Evaluate optional linear-light compositing and verify output signaling. | Reference patches blend correctly, existing projects keep their appearance, and alpha handling and physical 10-bit precision are verified. |
| P2 | Sign and notarize macOS releases. | A downloaded release opens on a clean Mac without a Gatekeeper workaround. |

## Workflow and performance

Proposals from the code and competitor review.

| Priority | Proposal | Intended benefit |
|---|---|---|
| P1 | Beyond the frame-time percentiles of the endurance log, expose CPU/GPU costs by layer and FX, memory, output waits and events associated with stalls. | Identify the actual bottleneck rather than relying on average FPS. |
| P1 | Compute render dependencies from active outputs and previews; cache dependency order and index layer IDs. Preserve feedback and transport behavior. | Avoid processing unused branches and repeating structural searches every frame. |
| P1 | Bound readback and sharing waits with a frame-wide output budget. Let a slow output retain its last frame. | Prevent waits from accumulating across outputs. |
| P1 | Prepare expensive upcoming cues selectively: media opening, first frames, shaders and GPU resources. Show Preparing / Ready / Failed and bound memory use. | Reduce stalls at GO without preloading the whole project. |
| P2 | Preview snapshot/preset differences, included values and restored drivers before update or recall. Extend media usage to stored snapshots, presets and sequence references. | Safer edits, replacements and cleanup. |
| P2 | Add timeline editing across multiple keys/tracks, passage duplication, markers and MIDI/OSC gesture recording. | Faster show programming. |
| P2 | Add an On Air workspace protecting sensitive edits and reducing preview/helper work. | Clearer operation and more rendering headroom. |
| P2 | Profile spectral-analysis peaks in the audio callback; evaluate bounded background FFT processing while keeping fast level detection. | Improve callback timing under load. |
| P3 | Separate the prepared editing state from the live Program state, with explicit transitions and isolated audio/external commands. | Rehearse or prepare the next scene without changing live outputs. Multiple monitors of the same state are not sufficient. |

Start with control ownership and profiling, then measured render/output fixes and cue preparation. Validate behavior and performance changes through GitHub CI and representative hardware measurements.

## Parameter control

Current behavior is described in [Parameter control](../control/parameters.md).

| Priority | Proposal | Acceptance criteria |
|---|---|---|
| P1 | Show the active writer beside each parameter, with competing drivers and pending recalls in its details. | Users can identify why a value changed without inspecting several panels. |
| P1 | Add explicit per-parameter takeover and return policies: temporary override, hold until release, or blend back to automation. | A MIDI gesture has a predictable lifetime; releasing it resumes the underlying timeline at its current position without stopping unrelated tracks. |
| P1 | Define whether takeover cancels, suspends or preserves delayed recalls. Expose that choice. | A queued recall cannot unexpectedly reclaim a held parameter. |
| P2 | Apply a consistent arbitration model to layer values, composition masters and interaction settings. | Simultaneous recall, timeline and input scenarios follow a documented rule across all targets. |
| P2 | Offer explicit Replace, Add and Multiply modes with a defined base value, evaluation order and clamping. | Users can combine automation and live modulation deliberately, including grouped RGB, without depending on list order. |

### Proposed design: control priorities

A possible design for the takeover, return and arbitration proposals above. Keep three concepts separate:

- **Control priority:** which eligible control wins for a parameter.
- **Rule precedence:** which matching rule defines that control order.
- **Ownership lifetime:** when a winning control releases the parameter.

**Profiles.** **Composition > Control Priorities** holds named profiles, each a reorderable default order with scoped exceptions. Example orders, highest first (none is a mandatory default):

| Profile | Control order |
|---|---|
| Live | Interactions > Timelines > Layer Animations > Presets > Snapshots |
| Playback | Timelines > Presets > Snapshots > Layer Animations > Interactions |
| Recall | Snapshots > Presets > Timelines > Layer Animations > Interactions |

An individual timeline, animation or interaction can override its class rank (a specific MIDI controller outranking audio interactions). Controls are referenced by stable IDs, with names displayed in the editor.

**Scoped rules.** A rule selects targets and replaces their control order or ownership settings; unspecified settings inherit from the profile. Scopes: composition, category (Source, Color, FX, Spatial), layer, layer and category, parameter, or OSC address pattern (`/layer/*/opacity`). Categories need explicit metadata, not OSC prefixes. Layer selections follow internal IDs across renames; **Include Descendants** covers group contents. `*` keeps OSC semantics (one path segment, no recursive `**`). The most specific rule wins; **Rule Priority** resolves equal or overlapping selectors, a stable rule ID breaks remaining ties, and ambiguous overlaps are flagged. The specificity comparison between category, layer and wildcard selectors must be formalized so results never depend on container iteration order.

Illustrative notation only, not an implemented file format:

```yaml
profile: Live
default_order: [interactions, timelines, layer_animations, presets, snapshots]
rules:
  - target: { layer: Background, category: Color }
    order: [presets, timelines, interactions, layer_animations, snapshots]
  - target: { osc: /layer/*/opacity }
    order: [timelines, interactions, presets, snapshots, layer_animations]
```

**Switching during a show.** Snapshots and sequence steps select **Control Profile: Inherit** or a named profile, which becomes active before that recall's values are submitted. Presets may select one too, off by default, so a color preset never changes the policy unexpectedly. Profile changes and their recalls apply atomically at an engine update boundary, in a deterministic command order. Policy selection is not itself an arbitrated parameter, and changing it never restarts running timelines or animations.

**Ownership and release.** Priority applies only to eligible candidates; a stored value must not own a parameter forever because its class ranks first.

| Mode | Lifetime |
|---|---|
| While Active | Owns the parameter during playback or a recall fade, then releases. |
| Until Released | Keeps its candidate until an explicit release or replacement. |
| Timeout | Releases after a configured interval without new input. |

Interaction **Jump / Pick-up** stays separate from ownership. Sparse MIDI/OSC messages need explicit hold or timeout semantics; continuous audio can stay active while sampled. Paused, stopped and completed animations need explicit behavior; a delayed recall becomes eligible at its deadline. On release, the current underlying candidate is revealed, optionally through a **Return Fade**. Hidden timelines and fades keep advancing. With no candidate left, a defined base value remains; how manual edits and completed recalls update it must be documented. Example: a timeline drives opacity, a higher-priority snapshot holds `0.8` Until Released; releasing it returns to the timeline's current value, optionally over 300 ms, without rewinding.

**Engine and interface requirements.**

- Show **Controlled by**, **Profile**, **Rule**, competing controls and **Release Control** beside each affected parameter, with rule-match previews.
- Evaluate candidate values first, then arbitrate and commit the winner. Reordering today's direct writes is insufficient: some writes cancel fades.
- Break ties within a class by explicit instance priority, activation order and stable IDs, not list placement.
- Treat grouped RGB as one candidate, with explicit channel overrides.
- Keep transport triggers, source replacement and topology changes outside scalar arbitration; preserve locks and safety controls independently.
- Cache rule resolution per parameter, invalidated on profile, rule, namespace or topology changes.
- Missing profiles or deleted targets get visible diagnostics and a defined fallback.

**Order.** 1) Specify rule precedence, base values and ownership transitions; introduce the central arbiter. 2) Add saved profiles, scoped rules and an inspector explaining the winning rule. 3) Add atomic snapshot/sequence profile selection, holds and return fades. This first stage selects a winner; Add/Multiply mixing remains the separate P2 proposal.

Keep existing project behavior until users opt into a new policy. Validate simultaneous writes, sparse MIDI versus continuous audio, pick-up, hold/release, RGB, delayed recalls, simultaneous profile changes, overlapping patterns, renamed/deleted targets and project round trips in GitHub CI.

## Competitor references

- [SMODE](https://doc.smode.io/userGuide/tutorialsAndHow-Tos/playbackAndExport/use-optimisation-tools-to-accelerate-a-composition/): layer/FX profiling and ON-AIR optimization.
- [Modulo Kinetic](https://www.modulo-pi.com/media-servers/modulo-kinetic/) / [Modulo Player](https://www.modulo-pi.com/markets/live-performances/): preparation, playback and show operation.
- [Millumin](https://help.millumin.com/docs/playback/dashboard/): dashboard, timeline workflow and Silent Edition.
- [MadMapper](https://madmapper.com/madmapper/faq): complete scenes, selective cues and control combination.
- [QLab audition](https://qlab.app/docs/v5/tools/auditioning-cues/) / [loading](https://qlab.app/docs/v5/other-cues/transport-cues/): isolated rehearsal and selective preparation.
