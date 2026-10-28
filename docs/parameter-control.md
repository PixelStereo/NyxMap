# Parameter control

Snapshots and presets store settings. Layer animations and timelines generate values over time. Interactions translate live input into parameter values. When they target the same parameter, their values are written in order; they are not automatically mixed.

## How the controls connect

```mermaid
flowchart LR
    subgraph Stored["STORE SETTINGS"]
        S["Snapshot<br/>Scene state and included controls"]
        P["Preset<br/>Selected settings and captured drivers"]
    end
    subgraph Time["GENERATE VALUES OVER TIME"]
        A["Layer animation<br/>Local keys or waveform"]
        T["Timeline<br/>Tracks on a shared clock"]
    end
    subgraph Live["REACT TO INPUT"]
        I["Interaction<br/>Audio, MIDI, OSC, keyboard<br/>or another parameter"]
    end
    S --> R["Recall<br/>Apply settings, delays and fades"]
    P --> R
    R --> V["Parameter value<br/>Source / Color / FX / Spatial"]
    A --> V
    T --> V
    I --> V

    classDef stored fill:#e8e6ff,stroke:#7060b8,color:#211a3d;
    classDef timed fill:#dff1ff,stroke:#397ca8,color:#142c40;
    classDef live fill:#dcf4e8,stroke:#39805c,color:#163526;
    classDef output fill:#fff0d4,stroke:#aa7930,color:#422c0e;
    class S,P stored;
    class A,T timed;
    class I live;
    class R,V output;
```

Arrows show possible writers, not addition or a permanent ownership hierarchy.

## Grouped XY parameters

ISF `point2D` inputs and native layer position, pivot, scale and corner offsets
offer an **XY** target alongside their separate **X** and **Y** components.
Presets can capture the pair or either component.

An XY interaction has two input chains: choose **X input and mapping** or
**Y input and mapping** to configure its source, ranges, filter, curve and
takeover independently. For a single OSC XY message, use the same address with
arguments `0` and `1`; separate addresses or protocols also work. A fresh X-only
event does not resend a stale Y value.

An XY animation is one track with a shared transport and two independent
curves or waves. Select X or Y to edit its keys, period, phase, center and
amplitude. **Drive axis** leaves either component available to another control.
Adding a separate X animation preserves the grouped Y animation, and vice versa.
Wave phase is measured in cycles: equal sine periods with X phase `0` and Y phase
`0.25` produce an ellipse. Each curve supports **Start from the current value**.

Open **XY path** in a local animation or timeline track to see the sampled
trajectory for the current pass. Blue marks the start, gold the end and green
the playhead. Click or drag along the path to seek the shared transport;
edit keys and wave settings through the X/Y selector. The preview follows
continuous wave time across loops and ping-pong, captured current-value starts,
and the live value of a disabled axis. Axis ranges are fitted independently:
this is a parameter plot, not a spatial preview of the composition.

Design references: [Chataigne Mapping 2D](https://benkuper.notion.site/Mapping-2D-Layer-ce3f908cae484a99acdd8b486c5de1fb)
and [ossia component addressing](https://ossia.io/score-docs/in-depth/unit-system.html).

| Control | What it does |
|---|---|
| Snapshot | Recalls scene state, including sources, FX, values, local animation states and interaction settings, subject to inclusion choices. |
| Preset | Recalls selected values and any stored categories, geometry or local drivers; it does not replace the whole scene. |
| Layer animation | Drives a layer parameter or RGB color with keys or a waveform. |
| Timeline | Drives one or more targets using a shared transport. |
| Interaction | Maps input through ranges, optional filtering, a curve and master level, then applies its takeover mode. |

## Current rules

The following order describes competing writes to an existing, unlocked **layer parameter** during a normal render frame. Commands can also write immediately between frames; this is not a transaction covering every control.

1. **Recalls and fades:** there is no fixed snapshot-versus-preset priority. A new recall replaces older active fades on the same addresses, starting from their current values; unrelated fades continue. Parameter delays defer the write until their deadline. Source or topology changes can affect a wider part of the layer.
2. **Layer animations:** playing local animations write next.
3. **Timelines:** playing timelines write after local animations. If multiple timelines write the same address, the later writer in the engine's timeline list wins that frame.
4. **Interactions:** eligible interactions write last. With several competing interactions, list order determines the last successful write, subject to filtering and takeover.

For layer values, a successful animation or interaction write removes that address from active recall fades. It **does not stop a competing timeline or cancel a pending delayed recall**. Manual parameter edits also release active fades without stopping automation. Composition masters and interaction-setting fades use separate paths; the layer fade-cancellation rule is not a universal ownership contract.

### Interaction takeover

| Mode | Behavior |
|---|---|
| Jump (default) | Applies the incoming mapped value directly. |
| Pick-up | Waits until the controller approaches or crosses the current target value. If another writer moves the target away from the last written value beyond tolerance, pick-up releases and must catch it again. |

MIDI, OSC and keyboard interactions normally write on fresh input or while their processed output changes. They do not permanently hold a value after one message. Audio is sampled every frame and can therefore keep overwriting automation. Internal interactions react to source changes. A grouped color interaction normally writes RGB; alpha requires explicit mapping. RGB pick-up waits for all mapped channels to meet their target.

### What a recall can change besides the value

- A snapshot restores included interaction settings and disables interactions absent from its stored list. An interaction present but excluded from recall stays unchanged.
- A snapshot restores local animation states. An animation playing both before and after recall continues its clock; an absent local animation stops. Global timelines are not automatically stopped by that recall.
- A preset can restore captured local animations and interaction definitions. These can immediately compete with its static target values.
- **Start from current value** captures the starting value when playback starts from stopped or is rewound. It does not continuously sample competing controls or grant priority.

### Collision example

Assume all four controls still target the same opacity, the recalls have no delay, and the interaction remains enabled in Jump mode:

| Writer | Value |
|---|---:|
| Preset target | 0.2 |
| Snapshot target | 0.8 |
| Playing timeline | 0.5 |
| Fresh MIDI input | 0.9 |

The interaction produces **0.9 for that frame** and releases the active opacity fade. Without another MIDI message or filter change, the timeline writes again on the next frame and can return opacity to **0.5**. A still-pending delayed recall can execute later. Winning one write does not mean owning the parameter.

## Countdown, chronometer and clock generators

The Media Bin lists **Countdown**, **Chronometer** and **Clock** under Generators, with Text and Timecode. Dropped onto a layer, each one is a text layer (same font, size, color, alignment, outline and shadow controls as Text) that shows a time. Their settings and transport are layer parameters under `clock/`: OSC addresses (`/layer/<name>/clock/...`), interactions, snapshots and presets reach them like any other value.

| Address | Generators | Meaning |
|---|---|---|
| `clock/target` | Countdown | `duration` or `time_of_day` (counts down to the next occurrence of `clock/target_time`, across midnight if needed). |
| `clock/duration` | Countdown | Seconds it counts down from. |
| `clock/target_time` | Countdown | Time of day, as text (`20:30:00`, `8:30 PM`). |
| `clock/offset` | Chronometer | Seconds it counts up from. |
| `clock/format`, `clock/custom_format` | All | Countdown and chronometer: `hh:mm:ss`, `mm:ss`, `ss`, with tenths (`.t`) or frames (`:ff`), or `custom` with tokens `h hh m mm s ss t tt ttt ff` and `'quoted text'`. Clock: `24h`, `12h`, or `custom` with a Qt date-time format. |
| `clock/fps` | Countdown, Chronometer | Frames per second for the `ff` token. |
| `clock/at_zero`, `clock/zero_text`, `clock/overtime_color/r,g,b,a` | Countdown | At zero: `stop` (holds 00:00), `text` (shows the text), or `overtime` (counts on below zero in the overtime color). |
| `clock/seconds`, `clock/date`, `clock/zone` | Clock | Seconds and date shown; time zone (empty: system, `UTC`, `+02:00`, `Europe/Paris`). `clock/zone` also applies to a countdown to a time of day. |
| `clock/play` | Countdown, Chronometer | Running or paused (stored in snapshots and presets). |
| `clock/elapsed` | Countdown, Chronometer | Seconds run since the start (stored; a recall leaves a clock that is already running untouched). |
| `clock/start`, `clock/pause`, `clock/reset` | Countdown, Chronometer | Actions. Reset returns to the start and pauses; starting a countdown that finished starts it again from its full duration. |
| `clock/restart` | Countdown, Chronometer | "Restart on recall": a snapshot or preset holding it on starts the clock from the beginning. |
| `clock/value`, `clock/zero` | Read only | The time shown in seconds; whether a countdown reached zero. An internal interaction can follow `clock/zero` to drive another parameter at zero. |

Countdowns and chronometers follow real time (not the composition speed). Only their own transport and **STOP ALL** pause them. A project saves the elapsed time and running state; on load, a running clock continues from the saved value.

## Line generator

The Media Bin also lists **Line** under Generators. It works like MadMapper's Line tool: you draw segments on a layer, then animate them along their length. Its shader is built into the application, so a project never loses it. The layer's source is saved as `/NyxMap/Generators/Line` with its lines, and there is no file to find, relink or collect.

**Drawing.** Dropping Line onto a layer starts drawing a line in the preview. **Draw Line** in its Source tab draws another; a layer holds as many lines as needed.
- Click each point. Enter, a double-click, or the first point again ends the line (the first point again closes it).
- Backspace takes the last point back. Esc cancels.
- Afterwards: drag a point to move it, drag a segment to move the whole line, double-click a segment to add a point. Delete removes the selected point (or the line, below two points) or the selected line.
- Every edit is undoable. **Delete All** removes them all.

The points are in the layer's own frame, so moving, scaling or warping the layer moves its lines with it.

**Animation.** These are the shader's inputs, layer parameters under `param/` (`/layer/<name>/param/rate`…), so they can be animated and reached from OSC, interactions, snapshots and presets. The Source tab's `speed` scales its time; Resolution sets its picture size, as for any ISF generator.

| Address | Meaning |
|---|---|
| `param/animation` | `0` Solid, `1` Fill, `2` Chase, `3` Strobe, `4` Pulse, `5` Media. |
| `param/lineColor/r,g,b,a` | The lines' color. With `param/gradient` on, it goes to `param/endColor` from each line's start to its end. |
| `param/backColor/r,g,b,a` | Off the lines and where they are not lit (transparent by default). |
| `param/thickness` | Pixels of the generator's picture. Joins and ends are round. `param/softness` feathers the edges. |
| `param/rate` | Cycles per second, either way: the fill's loop, the dashes' scrolling, the strobe's and the pulse's frequency, the media's scrolling. |
| `param/fill` | Fill with `rate` 0: the part lit from the start (animate it to grow the line). With a rate, the fill loops. |
| `param/dashes`, `param/dashWidth` | Chase: dashes along each line, each one a fraction of its period. |
| `param/duty` | Strobe: the part of each cycle lit. |
| `param/along` | `0` each line from its start to its end, `1` each segment on its own. |
| `param/reverse` | From the end to the start. |
| `param/cascade`, `param/cascadeBy` | Offset: each line (`0`) or each segment of a line (`1`) starts this fraction of a cycle after the one before it. |
| `param/media` | Media: an image or another layer's picture laid along the lines, its width along the path, its height across the thickness. |

**Pixel mapping.** **Fixtures on Lines** in the Source tab puts a fixture of the chosen one-pixel type (RGB, RGBW…) on each line. Each one has that many pixels spread evenly along its line, in DMX order from the line's first point, at the next free addresses. It follows its line when the line is edited or the layer moves. In the views it shows as a strip: its path and its pixels. It cannot be moved, resized or turned on its own. A fixture keeps its last place if its layer or line is removed. A layer inside a group is followed through its own mapping only, not its group's.

## Proposed improvements — not implemented

| Priority | Proposal | Acceptance criteria |
|---|---|---|
| P1 | Show the active writer beside each parameter, with competing drivers and pending recalls in its details. | Users can identify why a value changed without inspecting several panels. |
| P1 | Add explicit per-parameter takeover and return policies: temporary override, hold until release, or blend back to automation. | A MIDI gesture has a predictable lifetime; releasing it resumes the underlying timeline at its current position without stopping unrelated tracks. |
| P1 | Define whether takeover cancels, suspends or preserves delayed recalls. Expose that choice. | A queued recall cannot unexpectedly reclaim a held parameter. |
| P2 | Apply a consistent arbitration model to layer values, composition masters and interaction settings. | Simultaneous recall, timeline and input scenarios follow a documented rule across all targets. |
| P2 | Offer explicit Replace, Add and Multiply modes with a defined base value, evaluation order and clamping. | Users can combine automation and live modulation deliberately, including grouped RGB, without depending on list order. |

Keep existing project behavior until users opt into a new policy. Validate simultaneous writes, sparse MIDI versus continuous audio, pick-up, RGB, delayed recalls and release behavior in GitHub CI.

Implementation references: [frame order](../src/engine/Render.cpp), [animation evaluation](../src/engine/Animations.cpp), [recalls and fades](../src/engine/Snapshots.cpp), [interaction evaluation](../src/engine/InteractionsEngine.cpp), [takeover](../src/engine/Interactions.cpp).
