# Pinol · Open Stage Control

A live show desk with **SHOW**, **SNAPSHOTS**, **PRESETS**, **TIMELINES** and
**CONNECT** pages. The show page follows Pinol's selected sequence, with previous,
current and next cue cards, operator notes, elapsed/remaining time, phase meters,
GO, GO BACK and stop-waits controls. Libraries follow the open project and are
paged in groups of 24 to keep large shows responsive.

## Open it

1. Install [Open Stage Control 1.31.x](https://openstagecontrol.ammd.net/).
2. In Pinol **Settings**, enable **OSC / OSCQuery**. Its default TCP port is `5678`.
3. Download **Pinol-OpenStageControl** from a successful [controller workflow](https://github.com/PixelStereo/Pinol/actions/workflows/open-stage-control.yml)
   and extract the whole ZIP, including its `node_modules` folder. In the launcher,
   set **load** to `pinol.json` and **custom-module** to `pinol.js`, then start.
4. Open **CONNECT** to enter another Pinol computer or OSCQuery port. Open Stage
   Control itself can be opened from a tablet at its HTTP address, normally
   `http://<controller-computer>:8080`.

Equivalent command (adjust paths for your operating system):

```sh
open-stage-control --load /path/to/pinol.json \
  --custom-module /path/to/pinol.js --port 8080 --osc-port 11111
```

The controller pack includes its pure JavaScript `ws` and `osc` dependencies and
their license files. Keep them next to the session. Pinol
commands and feedback use **one OSCQuery/WebSocket connection**; no UDP feedback
target or npm installation is needed for the downloaded pack. Widgets' local `127.0.0.1:1234`
target is intercepted by the module, which sends their commands to the Pinol host
chosen on CONNECT.

For a source checkout, prepare the dependencies once with
`npm install --ignore-scripts --omit=optional` in this folder. This step is done
by GitHub CI when it builds the downloadable pack.

## Operating rules

- The connection status is authoritative. Show commands are disabled offline;
  they are dropped, never stored for replay after reconnection.
- Selecting a sequence selects it in Pinol. Other sequences may keep running.
- GO and GO BACK follow the sequence displayed when feedback was received. Cue
  buttons address their own sequence and runtime cue IDs.
- **STOP WAITS** cancels pending cues/follows for this sequence. **STOP ALL WAITS**
  requires a double tap and cancels them for every sequence. Neither stops media
  or timelines already playing. Use a timeline's STOP to stop that timeline.
- Snapshot/preset buttons recall immediately with the timing stored in Pinol.
  These recalls can compete with running controls; see [parameter control](../../docs/parameter-control.md).
- Timeline counters use animation seconds. `∞` means no finite end. Progress
  follows the current pass, including ping-pong. A paused/frozen timeline does
  not have a meaningful wall-clock completion countdown.
- Lists update after definition changes and periodically recover if an update
  was missed. IDs keep launches bound to the original object after list edits;
  deleted IDs are refused. Cue IDs are runtime identities: reload the catalog
  after loading a project, rather than saving cue IDs for use in another show.

See the [OSC API guide](../../docs/osc.md) for commands, schemas, subscriptions
and the optional legacy UDP cue feedback. Open Stage Control is a separate
GPL-3.0 application; it is not bundled or linked into Pinol.
