# Parameter control priorities

**Proposal for future implementation.** This document describes a possible design,
not available settings. See [parameter-control.md](parameter-control.md) for current
behavior.

## Goal

Choose which control drives a parameter when snapshots, presets, timelines, layer
animations and interactions compete. Store a default policy in each composition,
override it for selected targets, and switch policies during playback.

Keep three concepts separate:

- **Control priority:** which eligible control wins for a parameter.
- **Rule precedence:** which matching rule defines that control order.
- **Ownership lifetime:** when a winning control releases the parameter.

## Composition profiles

Add **Composition > Control Priorities**, with named profiles containing a
reorderable default order and scoped exceptions. Orders below are examples, with
the highest priority first; none is a mandatory default.

| Profile | Control order |
|---|---|
| Live | Interactions > Timelines > Layer Animations > Presets > Snapshots |
| Playback | Timelines > Presets > Snapshots > Layer Animations > Interactions |
| Recall | Snapshots > Presets > Timelines > Layer Animations > Interactions |

Allow an individual timeline, animation or interaction to override its class rank.
For example, a specific MIDI controller could outrank audio interactions. Refer to
controls by stable IDs, with names displayed in the editor.

## Scoped rules

Rules select targets and replace their control order or ownership settings. Keep
unspecified settings inherited from the profile default.

| Scope | Example |
|---|---|
| Composition | All parameters |
| Category | Source, Color, FX or Spatial |
| Layer | Background |
| Layer and category | Background / FX |
| Parameter | `/layer/Background/opacity` |
| OSC address pattern | `/layer/*/opacity` |

Use explicit category metadata: category membership cannot reliably be inferred
from OSC prefixes alone. Explicit layer selections follow internal IDs across
renames. Address patterns follow the current OSC namespace, with a preview of
matching targets. An **Include Descendants** option covers nested group contents.

Preserve OSC matching semantics: `*` matches within one path segment and does not
cross `/`. Do not silently introduce recursive `**` matching.

Prefer the most specific rule; expose **Rule Priority** to resolve equally
specific or overlapping selectors. Use a stable rule ID as the final tie-breaker,
and flag ambiguous overlaps in the editor. Before implementation, formalize the
specificity comparison between category, layer and wildcard selectors so results
never depend on container iteration order.

Illustrative notation only, not an implemented file format:

```yaml
profile: Live
default_order: [interactions, timelines, layer_animations, presets, snapshots]
rules:
  - target:
      layer: Background
      category: Color
    order: [presets, timelines, interactions, layer_animations, snapshots]
  - target:
      osc: /layer/*/opacity
    order: [timelines, interactions, presets, snapshots, layer_animations]
```

## Switching profiles during a show

Snapshots and sequence steps may select **Control Profile: Inherit** or a named profile.
Inherit leaves the active profile unchanged. A selected profile becomes active
before that recall's parameter values are submitted. A scene represented by a
snapshot or sequence step therefore changes policy without duplicating rules.

Presets may optionally select a profile, disabled by default. A color preset
should not unexpectedly change the composition's control policy.

Apply profile changes and their recalls atomically at an engine update boundary.
Serialize simultaneous policy selections through a deterministic command order.
Do not let policy selection itself become an arbitrated parameter or a feedback
loop. Changing priorities must not restart running timelines or animations.

## Ownership and release

Priority applies only to eligible candidates. A stored value must not own a
parameter forever simply because its writer class ranks first.

| Mode | Lifetime |
|---|---|
| While Active | Owns the parameter during playback or a recall fade, then releases. |
| Until Released | Keeps its candidate until an explicit release or replacement. |
| Timeout | Releases after a configured interval without new input. |

Keep interaction **Jump / Pick-up** separate from ownership. Sparse MIDI/OSC
messages need explicit hold or timeout semantics; continuous audio updates can
remain active while sampled. Specify paused, stopped and completed animation
behavior explicitly. A delayed recall becomes eligible at its execution deadline.

On release, reveal the current underlying candidate, optionally through a
**Return Fade**. Keep hidden timelines and fades advancing instead of deleting
them when another control wins. When no candidate remains, retain a defined base
value; document how manual edits and completed recalls update that base.

Example: a timeline drives opacity. A higher-priority snapshot holds `0.8` with
Until Released. The timeline keeps running underneath. Releasing the snapshot
returns to the timeline's current value, optionally over 300 ms, without rewinding.

## Feedback and engine requirements

- Show **Controlled by**, **Profile**, **Rule**, competing controls and
  **Release Control** beside each affected parameter. Include rule-match previews.
- Evaluate candidate values first, then arbitrate and commit the winning value.
  Reordering today's direct writes is insufficient: some writes cancel fades.
- Keep ties deterministic between controls of the same class, using explicit
  instance priority, activation order and stable IDs rather than list placement.
- Treat grouped RGB as one candidate. Define explicit channel overrides so an
  unrelated writer cannot accidentally supply one channel of a grouped color.
- Keep transport triggers, source replacement and topology changes outside scalar
  arbitration. Preserve locks and explicit safety controls independently.
- Cache rule resolution per parameter; invalidate on profile, rule, namespace or
  topology changes instead of matching wildcard strings for every write/frame.
- Existing projects retain legacy behavior until they opt in. Missing profiles or
  deleted targets need visible diagnostics and a defined fallback.

## Implementation order and acceptance

1. Specify rule precedence, base values and ownership transitions; introduce the
   central arbiter with a legacy compatibility mode.
2. Add saved profiles, scoped rules and an inspector explaining the winning rule.
3. Add atomic snapshot/sequence profile selection, holds and return fades.

Validate through GitHub CI: competing recalls and timelines, delayed values,
sparse input versus audio, pick-up, hold/release, RGB, simultaneous profile
changes, overlapping patterns, renamed/deleted targets and project round trips.
Keep Add/Multiply mixing as a separate proposal; this first stage selects a
winner rather than implicitly combining competing values.
