# Controls, Show Control and the remote

## Controls

The **Controls** tab (Shift+5), next to the Media Bin and the Interactions, holds the operator's own values: a **Slider**
(range, step, unit, default), a **Toggle**, a **Selector** (a list of choices), a **Button** (1 while it is held down) and a
**Color**. **+ New** makes one; **⋯** edits, duplicates, moves or deletes it, each one undo step. They are saved with the
project (not in snapshots: a control is the operator's, not the scene's).

A control is a value of the show, `control/<name>` (a color: `control/<name>/r`, `g`, `b`), like a layer's opacity. So:

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
  buttons, hold buttons, color pickers), and follows the show live: what moves in NyxMap moves on the phone, and back.
* **`http://<this machine>:8765/show`** is the show desk: see [Show Control](#show-control) below.

Safety, because it is a door into a show:

* it **plays the show and moves live values** (controls, cues, snapshots, presets, timelines, any layer's values); it cannot open,
  save or edit the project, and it reaches nothing else of the OSC namespace;
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
