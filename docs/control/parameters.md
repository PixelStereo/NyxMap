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
**Y input and mapping** to configure its source, ranges, filters and
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
| Timeline | Drives one or more targets using a shared transport. An event track fires an action (Source › Rewind, Show › GO, a snapshot's recall, a shader's event…) or sets a value (a switch such as Source › Play or Layer › Visible, a choice such as Blend Mode, a number a curve does not drive such as Source › Seek To) at each of its flags as the playhead reaches them; seeking does not fire them. |
| Interaction | Passes its source's value through an ordered list of filters (Remap, smoothing, Curve, Math…) and its master level to its target, then applies its takeover mode. |

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

### Picking a value of the show

An interaction's source (**Follows**), its targets and the values an action sets, a timeline track's target and the viewer's picture all use the same menu. It starts at the same roots, each with its elements below it: **Composition**, **Viewports**, **Layers** (as the bin has them, groups and 3D scenes included), **Interactions** (as their list has them, groups included) and **Timelines**.

A **timeline** offers its transport, **Speed**, **Loop Mode** and, for each track, the **Value** it gives now (an Internal interaction can follow it) and, for an oscillator track, its **Period**, **Amplitude**, **Center** and **Phase**. An **interaction** offers only what is in play for it: its On switch, its source's settings (a MIDI interaction's channel and number, not a key's), and for a Mapping its master level, its pick-up and each filter's settings.

Interactions can be put in **groups** (the ▤ button, or right-click › New Group) and dragged in and out, as layers are.

A video or audio layer exposes where it plays as two read-only values an Internal interaction can follow: `playhead` (**Source › Playhead (s)**, seconds into the media) and `progress` (**Source › Progress**, 0 at the in point, 1 at the out point). For example, map a video's Progress to another layer's Position X to move it along with the playhead. Neither is saved or animated.

### Show actions and layer switches

The show's own actions are values of the composition, so a key, a MIDI note, an OSC message, another value (Internal) or a timeline flag reaches them like any other target. Each one is addressed by the stable id of what it acts on, and the list follows the snapshots, presets, timelines and sequences as they are added, renamed or removed.

| Address | Kind | Meaning |
|---|---|---|
| `show/go`, `show/back`, `show/stop`, `show/stop_all` | Action | The selected sequence's GO, GO Back, Stop; Stop All stops every sequence. |
| `sequence/<id>/go`, `/back`, `/stop` | Action | One sequence, whatever is selected. |
| `snapshot/<id>/recall`, `preset/<id>/recall` | Action | Recalls it, with its fade. |
| `timeline/<id>/play`, `/pause`, `/stop`, `/rewind` | Action | A timeline's transport. |
| `timeline/<id>/playing` | Read only | Whether it plays (an Internal interaction can follow it). |
| `show/blackout`, `show/pause` | Switch | The blackout (with its default fade) and the pause. |

Every layer also has **Layer › Visible** (`enable`) and **Layer › Solo** (`solo`), the switches of the layer list, and a video or audio layer has **Source › Seek To (s)** (`seek`, written only). None of these is saved in snapshots or presets: a snapshot shows and hides its layers itself, with its fades. An action runs right after it is written, on the engine's thread, exactly as the button that does the same.

An Internal interaction can follow a switch (0 / 1) and a choice (its index: 0 for the first one).

### Interaction filters

A Mapping's **Filters** are a list in a frame, applied from the top, like a layer's effects: **+** adds one (of the kind chosen), a click on a filter selects it, **−** removes the selected one and **▲ ▼** move it; each has its own on / off, and a meter with the number of what comes out of it now. Their settings are number fields with no bounds (press and slide sideways, or click to type any value).

There is no range of its own: the value enters the first filter as the source gives it (a key 1 / 0, MIDI 0..1, OSC as sent, Internal in the value's own units, a choice as its index) and the target receives what comes out of the last one, scaled by the master level, in its own units — a number that value, a switch on from 0.5, a choice the one of that index, an action fired each time it goes up past 0.5. With no filter, the value goes through as it is. To fit one range into another, add a **Remap** (from **+** it starts from the source's usual range to the target's).

| Filter | Settings | What it does |
|---|---|---|
| **Remap** | In Min, In Max, Out Min, Out Max | From one range to another (either way round: Out Max below Out Min inverts), held within Out at its ends. |
| **Curve** | its keys | Shapes the value with a drawn curve (easing, Bézier, hold), 0..1 in, 0..1 out. |
| **Math** | operation, value *a* | `x + a`, `x − a`, `x × a`, `x ÷ a` (÷ 0 leaves it), modulo, power, min, max, absolute, floor / ceil / round to a step of *a*, **1 − x** (invert). |
| **Clamp** | Min, Max | Keeps the value between two values. |
| **Steps** | Steps | A few levels only, 0 and 1 included (3 steps: 0, 0.5, 1). |
| **Threshold** | Threshold, Margin | 1 above the threshold, 0 below it; the margin around it keeps it from flickering. |
| **Toggle** | Threshold | Each time the value goes up past the threshold, the other state (0, 1, 0…): a key or a pad, on then off. |
| **Average** | Window | The average of the last moments. |
| **Weighted Average** | Smoothing | Smoothed, about 95 % of the way in its time. |
| **Attack Release** | Attack, Release | Rises and falls at speeds of their own. |
| **Damper** | Response, Damping | A spring that follows, overshooting more or less. |
| **Pulse Detection** | Threshold, Decay | A pulse on each sudden rise, decaying. |
| **Delay** | Delay | The value of a moment ago. |
| **Speed** | Full scale, Smoothing | How fast the value moves, either way (1: the full scale per second). |

**Math › 1 − x**, a falling Curve or a Remap with its Out the other way round inverts.

Each filter's on / off and settings are values of the interaction — `interaction/<id>/filters/<n>/enable`, `/a`, `/b`, `/c`, `/d` (`<n>` from 1, in the order of the list; a Remap: In Min, In Max, Out Min, Out Max) — so presets, snapshots, OSC and timelines reach them; `interaction/<id>/filters` is the whole list (its kinds, order, operations and curves), set at once.

### Interaction modes: Mapping and Action

An interaction has a **Mode**. **Mapping** (the default) drives one target continuously through its filters and its master level. **Action** does not map; it tests the source's value and acts when the answer changes, as Chataigne's actions do:

- **Conditions** compare the value the source gives, in its own units (a key 1 / 0, MIDI 0..1, OSC as sent, Internal in the value's units: seconds for Playhead, 0 / 1 for a switch, the index for a choice), with `=`, `≠`, `>`, `≥`, `<`, `≤`, **in range** or **out of range** (both ends included, in either order). **True when** all of them hold (and) or any of them (or). With no condition, each value the source gives does the **If true** actions (a key: each press and each release).
- When the answer **becomes true**, the actions **If true** are done, in order; when it **becomes false**, the actions **If false**. Nothing is done while it stays the same, and the very first answer only acts if it is true. Edited conditions are tried again at once on an Internal source's current value. A key pressed and released in one frame gives both, in order.
- Each action sets a value of the show: fires an action (GO, a snapshot's recall, Source › Rewind…), sets a switch On or Off, a choice, or a number. A locked layer is left alone, and a fade running on the value lets go, as for a Mapping.
- A number can go there **over** a time (seconds; 0: at once), from where it is, with a curve (Linear, Ease In, Ease Out, Ease In-Out, Ease In Cubic, Ease Out Cubic) and the way its ramp says (an angle the shortest way round). When the answer changes midway, the other actions start from where the value got to. Anything else that moves the value meanwhile (the interface, a snapshot, OSC, an animation, a Mapping, another action) takes it over and the fade stops. An action, a switch or a choice is set at once.
- The list shows **true** / **false** in its Value column; the settings show the input and whether the conditions hold now. A snapshot keeps an Action's on / off state, not its conditions or actions. Switching back to Mapping keeps the Action's settings for later.

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

Implementation references: [frame order](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Render.cpp), [animation evaluation](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Animations.cpp), [recalls and fades](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Snapshots.cpp), [interaction evaluation](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/InteractionsEngine.cpp), [takeover](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Interactions.cpp).
