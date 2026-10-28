# NyxMap · Stream Deck with Companion

Use the [NyxMap Companion module](../integrations/companion/README.md) to build a
show desk by dragging ready-made presets onto Stream Deck buttons.

## Installation

Download **NyxMap-Companion** from a successful
[Companion module workflow](https://github.com/PixelStereo/NyxMap/actions/workflows/companion.yml).
Extract the ZIP, import its `.tgz` with **Modules → Import module package**, and
add **Pixel Stereo: NyxMap** as a connection. Requires Companion 4.2+ with Node 22
module support. This is a custom package; it is not yet in Bitfocus's store.

Enable OSC / OSCQuery in NyxMap Settings. Configure `127.0.0.1:5678` on the same
computer, or NyxMap's LAN IP from another computer. Then open Companion's Presets
pane and drag the module's buttons onto your Stream Deck layout.

## Suggested 15-key layout

| | | | | |
| :---: | :---: | :---: | :---: | :---: |
| **SEQUENCE**<br>Current name | **PREVIOUS**<br>Cue / name | **CURRENT**<br>Cue / progress | **NEXT**<br>Cue / name | **STATUS**<br>Connected / offline |
| **BACK** | **ELAPSED**<br>`0:04.2` | **REMAINING**<br>`0:07.8` | **PHASE**<br>Action / wait | **GO** |
| **STOP WAITS** | **HOLD 2s**<br>Stop all waits | **PAUSE** | **BLACKOUT** | **REFRESH** |

Put project-specific sequences, cues, snapshots, presets and timelines on other
pages. Sequence buttons select; GO starts the selected sequence's next cue.
Direct cue buttons launch that cue immediately, with its configured waits.

The current cue uses green, the next cue red, pre-wait yellow and post-wait blue,
matching NyxMap. Small progress bars leave the time and label legible. Monitoring
buttons do nothing when pressed.

## Timing and control

Times and phases come from NyxMap. Cue elapsed/remaining and timeline animation
time remain distinct; `∞` means unbounded. Companion batches changed values at
up to 10 Hz, without fetching/rebuilding catalogs on each timing update.

STOP WAITS cancels pending waits/follows; it does not stop running media or
timelines. The all-sequence stop preset requires a two-second hold. Recalls and
live automation still follow the [parameter-control rules](parameter-control.md).

Offline actions are dropped, monitoring values clear and the buttons turn red.
Library IDs survive list reordering; deleted IDs are refused. Direct cue buttons
must be rebuilt after a project reload because cue IDs are runtime identities.

The project preset gallery is bounded by the connection setting; action dropdowns
still expose the full catalog. See the [complete help](../integrations/companion/companion/HELP.md)
for variables, configuration and troubleshooting, and the [OSC API](osc.md) for
the protocol used by the module.

Module installation follows Bitfocus's [module package workflow](https://companion.free/user-guide/beta/config/modules/).
