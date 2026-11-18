# Controls, Show Control and the remote

## Controls

The **Controls** tab (Shift+5), next to the Media Bin and the Interactions, holds the operator's own values: a **Slider**
(range, step, unit, default), a **Toggle**, a **Selector** (a list of choices), a **Button** (1 while it is held down) and a
**Color**, an **ADSR** (an envelope to take in the hand, see below), a **Multi-slider** (a row of 2 to 32 sliders in one control), a **Multi-point pad** (1 to 16 points on one square) and a **2D Slider** (a square: two numbers, X and Y, moved by one gesture; double-click puts it back to its
default). **+ New** makes one; **⋯** edits, duplicates, moves or deletes it, each one undo step. They are saved with the
project (not in snapshots: a control is the operator's, not the scene's).

**Right-click any parameter › Add to Controls Page › Page, or a group**: a control is made for it (a slider with the parameter's range,
a switch, a selector, a button or a color) with the interaction that makes it drive the parameter, on the page or in the group
chosen from the list of groups.

A new project starts with an empty Controls panel; its controls belong to the project and are cleared when you create a new project. **Group** (also in **+ New**) is a titled frame that lays its controls out in 1 to 4 columns. **⋯ › Move to Group** puts a
control or another group into one (or back on the page), **⋯ › Width** sets how many of the group's columns a control takes,
**⋯ › Columns** the group's own, and **Move up / down** orders what is beside it. The remote's `/control` page shows the same
groups, columns and widths.

An **ADSR** is four numbers moved by one curve with four points: the attack, the decay, the sustain (the level held) and the release.
Its times go up to its **Longest time** (⋯ › Edit). Each number is a parameter, `control/<name>/attack`, `decay`, `sustain` and
`release`, so a timeline, an interaction or a preset reaches it, and OSC sets the four at once: `/control/<name>` takes four floats.
On the remote page it is a tile with the same curve, a handle for each number. An interaction's **Envelope** filter can take its
four numbers from it (**Settings from** under its curve): the envelope of a hit is then moved from the page, a timeline or a preset.

A **Multi-slider** is a row of sliders sharing one range: each is a parameter, `control/<name>/1`, `/2`…, and OSC takes all of them in
one message (a float each). On the remote page the bars are vertical or horizontal (**Widget**), and a finger swept across them sets
each bar it crosses; the number of sliders and the range are set in its settings.

A **Multi-point pad** is a square with several points, each an X and a Y: `control/<name>/p1/x`, `/p1/y`, `/p2/x`… (OSC takes
x1, y1, x2, y2… in one message). On the remote page every finger takes the nearest free point, so several move at once. A point
drives what you decide: **right-click a parameter › Control with › the pad › Point 2 X** (or Y) makes the parameter follow that
coordinate, its range remapped; a point can drive an XY parameter with its X and its Y, or two distinct parameters. The same menu
reaches any control's number (a slider, a multi-slider's bar, an ADSR's attack…).

A control is a value of the show, `control/<name>` (a color: `control/<name>/r`, `g`, `b`; a 2D slider: `control/<name>/x` and
`y`, a point a timeline draws as an XY track), like a layer's opacity. So:

* an **interaction follows it** (Source *Internal*, *Controls › name*) to drive anything: one control read by several
  interactions drives several things at once, each with its own filters (a Remap to invert, an Envelope, a Curve);
* an **interaction drives it** (a key, a MIDI fader, an audio band, a hit as the source; the control as the target), so
  several gestures meet in one value;
* a **timeline animates it** (sliders and colors), an event track sets a switch;
* **OSC** reads and sets it: `/control/<name>` (a slider: float in its range; a toggle or a button: 0 / 1; a selector: 1 is the
  first choice; a color: three floats 0..1), and OSCQuery lists it;
* the **remote** moves it.

The address is made from the name when the control is created and does not follow a rename (an interaction or a remote knows
it by that address); duplicates get `_2`.

A **Button** is a gate, not a trigger: its value is 1 while it is held. Feed it to an *Envelope* filter for a flash that fires
on the press and releases on the let-go.

## The remote: two web pages

**Settings › OSC › Remote**: tick *Serve the Show Control and the Controls as web pages*, then open an address shown there on a
phone, a tablet or a browser on the same network, and enter the **PIN**. Each page links to the other; `http://<this machine>:8765/`
leads to `/control`.

* **`http://<this machine>:8765/control`** shows each control as something a thumb can use (sliders, big switches, selector
  buttons, hold buttons, color pickers, 2D sliders), and follows the show live: what moves in NyxMap moves on the phone, and back.
* **`http://<this machine>:8765/control`** is laid out as you want it: a free grid of 12 columns, **a page (a tab) for each group**
  (what is not in a group is the first page). **Edit** turns the layout on: drag a tile to move it (the tiles it lands on make room),
  drag its corner to resize it, and the **⚙** of a tile opens its settings: its name, its **widget** (a slider as a fader, a vertical
  fader, an encoder or a number; a selector as buttons or a menu; a button held or tapped), its **accent** color, and for a slider
  its **range, step, unit and curve** (fine at the low or at the high end). A tile can be deleted there too (its OSC address goes
  with it). The layout and the settings are saved with the project and are the same on every device; on a phone the page is one
  column in the order of the grid, without Edit. It runs on tablets of 2013 (iOS 12) and later.
* **`http://<this machine>:8765/show`** is the show desk: see [Show Control](#show-control) below.

Safety, because it is a door into a show:

* it **plays the show and moves live values** (controls, cues, snapshots, presets, timelines, any layer's values), and in Edit mode
  it lays out, sets and deletes **controls**; it cannot open or save the project, edit anything else in it, and it reaches nothing
  else of the OSC namespace;
* a **PIN** of 4 to 32 letters or digits (made at first use, **Random** to change it), compared in constant time; eight wrong
  codes lock an address out for a minute. Without a code, anyone on the network can play the show;
* the WebSocket is accepted only from a page this server served (same origin), so a page of another site cannot reach it;
* each page's script and style are the only code its Content-Security-Policy allows (hashes of exactly what is served);
* at most 16 clients, frames of at most 4 KB, 300 messages a second each;
* **no encryption**: the PIN and the values cross the network in clear, as OSC does. Use it on the show's network, not on a
  shared one. It listens where OSC does (*Listen on*).

It is off by default. To write your own client, the protocol is a WebSocket at `/ws?pin=<PIN>` with JSON text frames
(described in `src/engine/RemoteServer.h`).

## Show Control

The **Show Control** tab (Shift+6) and the `/show` page are the same desk, to play a show from:

* **SHOW**: the selected sequence; its previous, current and next cues, with their text, their times and how far they are; GO,
  GO BACK, STOP WAITS and STOP ALL WAITS (twice, for every sequence); the elapsed and remaining time; the cue stack, where a click
  plays a cue. Each cue tells how the next one starts: **WAIT · GO** (manual) or **AUTO** (after its end plus its post-wait).
* **SNAPSHOTS**, **PRESETS**: a button each, following the open project.
* **TIMELINES**: play, pause and stop, with their time and pass.
* **LAYERS** (the web page only: in NyxMap, *LAYER CONTROL →* leads to the Layers tab): any layer's values by section, live.
  Double-click or double-tap a name marked ↺ to reset it.
