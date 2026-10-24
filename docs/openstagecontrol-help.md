# Open Stage Control · connection help

Loading `pinol.json` displays the desk. Connecting it to Pinol also requires
`pinol.js` as the **custom-module**. Both files and their portable dependencies
are supplied in the repository: **no npm or separate Node.js installation needed**.

## Recommended setup

1. Install [Open Stage Control 1.31.x](https://openstagecontrol.ammd.net/).
2. If you already cloned Pinol, run `git pull` in **your own repository**, then
   open `integrations/open-stage-control`. Use this folder directly.
   Without Git, choose **Code → Download ZIP** on the
   [Pinol repository](https://github.com/PixelStereo/Pinol), extract it and open
   `integrations/open-stage-control`.

   You can also download `Pinol-OpenStageControl.zip` from **Assets** on the
   [latest release page](https://github.com/PixelStereo/Pinol/releases/latest).
   Older releases may not include this ZIP; the repository folder works directly.
   Keep the ready-to-load files together:

   ```text
   open-stage-control/             # or Pinol-OpenStageControl/ from a release
     pinol.json
     pinol.js
     pinol-layers.cjs
     pinol-dependencies.cjs
     pinol-dependencies.LICENSE.txt
   ```

3. Start Pinol. In **Settings → OSC**, enable **Control by OSC and publish the
   namespace (OSCQuery)**. Keep **OSCQuery port** at `5678` unless already changed.
4. In the Open Stage Control launcher, stop the server with the square button
   before editing its options. Set these two separate fields:

   | Launcher option | File from the extracted folder |
   | :--- | :--- |
   | **load** | `/your/Pinol/integrations/open-stage-control/pinol.json` |
   | **custom-module** | `/your/Pinol/integrations/open-stage-control/pinol.js` |

5. Start the server with the triangle button. On the same computer, the desk
   should show **CONNECTED · 127.0.0.1:5678** and the open project's cue list.
   The **CONNECT** page changes the Pinol host and OSCQuery port; it cannot load
   a missing custom module.

For a release ZIP, use the same filenames in its extracted folder. Commands and feedback share the
OSCQuery WebSocket connection; no UDP feedback target is needed for this desk.

## Computers and ports

| Connection | Address / default port |
| :--- | :--- |
| Desk → Pinol on the same computer | `127.0.0.1:5678` |
| Desk → Pinol on another computer | Pinol computer's LAN IP, port `5678` |
| Tablet browser → Open Stage Control | `http://<controller-computer>:8080` |
| Other OSC controllers → Pinol over UDP | Port `1234`; not the desk's OSCQuery port |

For another computer, choose a reachable interface in Pinol's **Listen on**
setting. `127.0.0.1` always means the computer running the Open Stage Control
server, including when a tablet displays its interface. If LAN access fails,
check that the selected interface and firewall allow the relevant TCP port.

## Edit layers

On **LAYERS**, choose a layer/group/output, then its section. Controls adapt to
its source, color, transform, FX and playback capabilities. Color pickers keep
individual RGB(A) fields; XY pads keep independent X/Y entry. Edits are live.
All browsers share the remote selection, without changing Pinol's inspector.
A deleted selection is cleared rather than replaced with another layer.
SHOW also has a **LAYER CONTROL →** shortcut. If neither the shortcut nor the
LAYERS tab appears, stop the server and update **both** launcher paths to the
same freshly extracted pack. Loading an older `pinol.json` keeps the old pages
even with a newer custom module.

Commit text/numeric fields with Enter or by leaving the field; Shift+Enter adds
a text line. File paths belong to the Pinol computer. Locks and read-only values
are respected. Existing automation can overwrite manual edits; see
[parameter control](parameter-control.md). The remote does not upload files or
add/remove layers and FX.

## Troubleshooting

| Message or symptom | Fix |
| :--- | :--- |
| **Load pinol.js as the custom module** | Set **custom-module** to `pinol.js`, restart the server, and check the launcher log for module errors. |
| **Cannot find module 'ws'** or **'osc'** | You are loading an older module. Run `git pull`, then point both launcher fields to the current repository folder. Current files include their runtime dependencies. |
| **Cannot find module …pinol-dependencies.cjs** | Restore the complete current folder, including the dependency bundle beside `pinol.js`. |
| **Cannot find module …pinol-layers.cjs** | Extract the complete current pack; keep this helper next to `pinol.js`. |
| **Update Pinol to use the layer remote** | Update Pinol to a build publishing `layers` in `/show/catalog`. Other remote pages remain usable. |
| **Could not open session file**, **Unexpected token**, or **not valid JSON** | **load** must point to `pinol.json`. JavaScript belongs in **custom-module**. |
| **ECONNREFUSED** or persistent **DISCONNECTED** | Start Pinol, enable OSC / OSCQuery, and match its host and OSCQuery port. Use `5678`, not UDP `1234` or the desk's HTTP `8080`. |
| **Update Pinol: show-control API unavailable** | Use a Pinol build that includes `/show/catalog` and `/show/state`. |

To distinguish a Pinol connection problem from a launcher/module problem, open
these read-only URLs on the controller computer, adjusting the host and port:

- [Server information](http://127.0.0.1:5678/?HOST_INFO) should identify Pinol.
- [Show catalog](http://127.0.0.1:5678/show/catalog?VALUE) should return the project's catalog.

If both work but the desk remains offline, inspect the Open Stage Control
launcher log and the **custom-module** path first.

## Updates

Run `git pull` in your Pinol repository, or extract a new release ZIP. Stop and
restart the Open Stage Control server to reload the updated module and session.
Keep **load** and **custom-module** pointed at files from the same version.
Dependency rebuilding is only for contributors; see
[maintainer instructions](../integrations/open-stage-control/README.md#maintaining-the-dependency-bundle).

See [desk operation](../integrations/open-stage-control/README.md#operating-rules)
and the [OSC API](osc.md) for commands and feedback.
