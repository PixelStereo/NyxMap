# Concepts

The few ideas the rest of this documentation builds on.

## Composition and layers

A project holds a **composition**: the picture sent to the outputs. It is a stack of **layers**, each with a source and its own settings, grouped into tabs in the inspector:

| Tab | What it holds |
|---|---|
| **Source** | What the layer shows or plays: a media file, a [generator](../visuals/generators.md), a camera, a [stream](../visuals/media.md#network-streams), a [3D scene](../visuals/3d-scenes.md), a sound or an [audio generator](../audio/generators.md) |
| **Color** / **FX** | Color correction and the effect chain (ISF shaders) |
| **Spatial** | Placement and mapping: corner pin, mesh warp, shared Zones |
| **Audio FX** | The layer's [audio effect chain](../audio/effects.md) |
| **Anim** | The layer's own [animations](../control/animations.md) |

Layers can be grouped, and a layer can reuse another layer's picture before or after its effects.

## Everything is a parameter

Every setting of a layer, of the composition or of an interaction is a **parameter** with an address such as `/layer/Background/opacity`. The same address is used by:

- **snapshots and presets**, which store values and recall them, with delays and fades;
- **animations and timelines**, which generate values over time;
- **interactions**, which turn live input (audio, MIDI, OSC, keyboard, camera, another parameter) into values;
- the [OSC API](../reference/osc.md), the [remote](../show/controls-and-remote.md) and [Companion](../integrations/companion.md).

When several of them write the same parameter, the order is defined in [Parameter control](../control/parameters.md).

## The show

- A **snapshot** stores the state of the scene; a **preset** stores selected settings.
- A **sequence** chains steps (snapshots, presets, actions) fired by **GO** or automatically, optionally on timecode.
- **Controls** are the operator's own sliders, toggles and buttons, read by interactions.

See [Editing snapshots, presets and sequences](../show/editing.md) and [Controls, Show Control and the remote](../show/controls-and-remote.md).

## The main window

The left side holds tabs, each with a shortcut:

| Tab | Shortcut |
|---|---|
| Media Bin | ++shift+1++ |
| Layers | ++shift+2++ |
| Interactions | ++shift+3++ |
| Event log (OSC, MIDI, keyboard) | ++shift+4++ |
| Controls | ++shift+5++ |
| Show Control | ++shift+6++ |

Drag a media from the **Media Bin** onto a layer to play it. The inspector on the right shows the selected layer's tabs; the **Composition** tab holds the outputs, [DMX](../visuals/pixel-mapping.md) and 3D quality settings.
