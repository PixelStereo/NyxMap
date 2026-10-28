# NyxMap

## Install and connect

1. Use Companion 4.2+ with Node 22 module support. Download the **NyxMap-Companion**
   artifact from NyxMap's successful Companion module workflow and extract its ZIP.
2. In Companion's **Modules** page, choose **Import module package** and import
   the enclosed `.tgz`. Add the **Pixel Stereo: NyxMap** connection.
3. In NyxMap Settings, enable **Control by OSC and publish the namespace (OSCQuery)**.
4. Configure the connection with the NyxMap host and **OSCQuery TCP port**, normally
   `127.0.0.1` and `5678` on the same computer. On another computer, use NyxMap's LAN
   IP and a reachable **Listen on** interface.
5. Once connected, open Companion's **Presets** pane and drag NyxMap presets onto
   Stream Deck buttons. No UDP feedback target or Open Stage Control is required.

## Presets

| Category | Buttons |
| :--- | :--- |
| Transport | GO, BACK, STOP WAITS, hold 2 seconds for STOP ALL WAITS, composition pause, blackout, refresh. |
| Monitor | Selected sequence, previous/current/next cue, elapsed, remaining, phase, connection. |
| Project | Sequence selection, direct cue GO, snapshot/preset recall, timeline play with time/progress. |

Green identifies the current cue, red the next cue, yellow pre-wait and blue
post-wait. Progress bars sit below the text. Timers come from NyxMap, rather than
being extrapolated by Companion. Monitoring buttons have no press action.

The gallery is limited to 96 project buttons by default; configure `0…512` in the
connection settings. All catalog entries remain available in action dropdowns,
even when their ready-made buttons fall outside that gallery limit. Existing
placed buttons keep their saved actions; refreshing updates the preset gallery.

## Operating rules

- GO/BACK/STOP target the sequence displayed by the latest feedback. A specific
  sequence can also be chosen in the action options. Selecting does not start it
  or stop other sequences.
- **STOP WAITS** cancels pending waits/follows. It does not stop media or timelines
  already playing. Use timeline STOP or a deliberately prepared blackout cue.
- The STOP ALL preset runs only after a two-second hold. Its underlying action
  also requires the confirmation option.
- Snapshot/preset recalls use timing stored in NyxMap and can compete with active
  automation. See NyxMap's parameter-control documentation.
- Offline buttons turn red and counters show `--`. Offline commands are rejected
  and never replayed on reconnect. Stable IDs survive list reordering; deleted
  targets fail instead of launching a different item.
- Cue IDs are runtime identities. Recreate direct cue buttons after reloading a
  project. Review saved library buttons when switching to a different project.
- Timelines use animation seconds; `∞` is unbounded. Pauses and speed changes
  affect completion time. These counters are not independent wall-clock timers.

## Variables and actions

Variables use the connection's label: `$(NyxMap:current_elapsed_text)`,
`$(NyxMap:current_remaining_text)`, `$(NyxMap:current_phase)`,
`$(NyxMap:sequence_name)` and corresponding `previous_*` / `next_*` fields.
Each timeline adds `timeline_<id>_position_text`, `_remaining_text`, `_state`,
`_progress` and other numeric fields. Use Companion's variable picker.

Custom buttons can select a sequence, GO directly to a cue, recall a snapshot or
preset, play/pause/stop/rewind a timeline, pause the composition or toggle blackout.
Feedbacks provide connection state, sequence selection, cue role/phase, timeline
playback, pause/blackout status and cue/timeline progress bars.

If connection fails, verify `http://<NyxMap-host>:5678/?HOST_INFO` and
`http://<NyxMap-host>:5678/show/catalog?VALUE`. Port `1234` is UDP OSC, not OSCQuery.
An unavailable catalog means NyxMap needs updating or OSCQuery is disabled.
