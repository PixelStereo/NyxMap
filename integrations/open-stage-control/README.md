# NyxMap · Open Stage Control

A live show desk with **SHOW**, **SNAPSHOTS**, **PRESETS**, **TIMELINES**,
**LAYERS** and **CONNECT** pages. The show page follows NyxMap's selected sequence, with previous,
current and next cue cards, operator notes, elapsed/remaining time, phase meters,
GO, GO BACK and stop-waits controls. Libraries follow the open project and are
paged in groups of 24 to keep large shows responsive.

Every cue has a dedicated continuation badge, also shown on previous/current/next
cards: **WAIT · GO** (amber) requires a manual GO; **FOLLOW** (teal) advances after
the action starts plus post-wait; **AUTO-FOLLOW** (lavender) advances after the
action ends plus post-wait. FOLLOW can overlap actions. AUTO-FOLLOW holds on an
endless action. The badge belongs to the cue that schedules the next GO; the next
cue's own pre-wait then applies. The last cue wraps only if the sequence loops.
Start/end delays stay visible beside each cue, independently of live timing.

## Open it

For launcher settings, ports and connection errors, see the
[connection help](../../docs/openstagecontrol-help.md).

1. Install [Open Stage Control 1.31.x](https://openstagecontrol.ammd.net/).
2. **If you already have the NyxMap repository:** run `git pull`, then use this
   `integrations/open-stage-control` folder directly. It is ready to load;
   **no npm installation or separate Node.js installation is required**.
   **Without Git:** download the [repository ZIP](https://github.com/PixelStereo/NyxMap/archive/refs/heads/main.zip)
   using GitHub's **Code → Download ZIP**, extract it and open that same folder.
   Packaged releases also provide `NyxMap-OpenStageControl.zip` under
   **[Releases → Assets](https://github.com/PixelStereo/NyxMap/releases/latest)**.
   Older releases may not contain the remote ZIP; use the repository files in that case.
3. In NyxMap **Settings**, enable **OSC / OSCQuery**. Its default TCP port is `5678`.
4. Stop the Open Stage Control server before changing the launcher settings:

   | Launcher field | File from the same remote folder |
   | :--- | :--- |
   | **load** | `nyxmap.json` |
   | **custom-module** | `nyxmap.js` |

   Keep `nyxmap-layers.cjs`, `nyxmap-dependencies.cjs` and
   `nyxmap-dependencies.LICENSE.txt` beside them. Restart the server.
5. Open **CONNECT** to enter another NyxMap computer or OSCQuery port. Open Stage
   Control itself can be opened from a tablet at its HTTP address, normally
   `http://<controller-computer>:8080`.

Equivalent command, using your repository's actual location:

```sh
open-stage-control --load /path/to/NyxMap/integrations/open-stage-control/nyxmap.json \
  --custom-module /path/to/NyxMap/integrations/open-stage-control/nyxmap.js \
  --port 8080 --osc-port 11111
```

The pure JavaScript WebSocket and OSC dependencies are included in
`nyxmap-dependencies.cjs` in both the repository and release ZIP. Their license
texts are in `nyxmap-dependencies.LICENSE.txt`. A `node_modules` folder is not
needed at runtime. NyxMap commands and feedback share **one OSCQuery/WebSocket
connection**; no UDP feedback target is required. Widgets' local `127.0.0.1:1234`
target is intercepted by the module, which sends commands to the NyxMap host on CONNECT.

## Operating rules

- The connection status is authoritative. Show commands are disabled offline;
  they are dropped, never stored for replay after reconnection.
- Selecting a sequence selects it in NyxMap. Other sequences may keep running.
- GO and GO BACK follow the sequence displayed when feedback was received. Cue
  buttons address their own sequence and runtime cue IDs.
- **STOP WAITS** cancels pending cues/follows for this sequence. **STOP ALL WAITS**
  requires a double tap and cancels them for every sequence. Neither stops media
  or timelines already playing. Use a timeline's STOP to stop that timeline.
- Snapshot/preset buttons recall immediately with the timing stored in NyxMap.
  These recalls can compete with running controls; see [parameter control](../../docs/parameter-control.md).
- Timeline counters use animation seconds. `∞` means no finite end. Progress
  follows the current pass, including ping-pong. A paused/frozen timeline does
  not have a meaningful wall-clock completion countdown.
- Lists update after definition changes and periodically recover if an update
  was missed. IDs keep launches bound to the original object after list edits;
  deleted IDs are refused. Cue IDs are runtime identities: reload the catalog
  after loading a project, rather than saving cue IDs for use in another show.

## Layer remote

Choose a layer, group or output on **LAYERS**, then choose its section. Selection
is shared by connected browsers and independent of NyxMap's inspector selection.
The editor discovers the selected object's OSCQuery controls: general settings,
source and shader parameters, text/clocks, color, transform, each FX, playback,
output routing and existing parameter animations. Sections appear when available.

RGB(A) controls combine a color picker with individual channel fields. XY controls
combine a pad with independent X/Y fields, in NyxMap's native units. Numeric values
have precise entry and a slider when a range is declared; choices use menus.
Press Enter or leave a text/numeric field to commit. Text areas accept Shift+Enter
for a new line. A source file path refers to the **NyxMap computer's filesystem**.

Edits act immediately and can compete with presets, interactions and timelines.
NyxMap supplies live values and enforces locks. Only visible controls subscribe
to feedback; switching sections releases previous subscriptions. Stable layer
IDs preserve targets after renaming/reordering. Removing the selected layer
clears the selection; stale editor commands are discarded.

**Reset a parameter:** double-click its name (double-tap on touchscreens) when
marked **↺**. One click does nothing. A color/XY title resets the whole value;
a channel label such as R or X resets only that component. NyxMap resolves its
current declared default at the time of the command, including source and FX
defaults; the value captured when connecting is never used as a substitute.
Read-only values, triggers and controls without a declared reset have no marker.
Resets are disabled offline and while the layer is locked. Existing automation
may overwrite a reset, just as it can overwrite another manual edit.

The remote edits controls published by OSC; it does not create layers, add/remove
FX or upload media. Keep `nyxmap-layers.cjs` beside `nyxmap.js` in the full pack.
**LAYER CONTROL →** on SHOW opens this same editor without launching a cue.

See the [OSC API guide](../../docs/osc.md) for commands, schemas, subscriptions
and the optional legacy UDP cue feedback. Open Stage Control is a separate
GPL-3.0 application; it is not bundled or linked into NyxMap.

The cue stack follows NyxMap’s palette: green for the current cue, red for the
next cue, yellow for pre-wait and blue for post-wait. Previous cues are dimmed.
Progress fills the cue background in translucent phase color; the current cue
has the strongest visual weight. Cues stay ordered vertically on every screen.

## Maintaining the dependency bundle

Only contributors changing dependencies need Node.js/npm:

```sh
npm ci --ignore-scripts --omit=optional
npm run build:dependencies
```

Commit `package-lock.json`, `nyxmap-dependencies.cjs` and its license file together.
CI checks that the committed bundle matches the locked dependencies, exercises
an isolated copy and the actual Open Stage Control client, then packages the
same ready-to-load files. Tag builds attach the tested ZIP to the NyxMap release.
