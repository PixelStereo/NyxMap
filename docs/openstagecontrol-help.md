# Open Stage Control · connection help

Loading `pinol.json` displays the desk. Connecting it to Pinol also requires
`pinol.js` as the **custom-module**, with its dependencies installed beside it.

## Recommended setup

1. Install [Open Stage Control 1.31.x](https://openstagecontrol.ammd.net/).
2. Download **Pinol-OpenStageControl** from a successful
   [controller workflow](https://github.com/PixelStereo/Pinol/actions/workflows/open-stage-control.yml).
   Extract the artifact and the enclosed `Pinol-OpenStageControl.zip` completely.
   Keep this folder together; copying only the JSON and JavaScript files is insufficient:

   ```text
   Pinol-OpenStageControl/
     pinol.json
     pinol.js
     node_modules/
       ws/
       osc/
       …
   ```

3. Start Pinol. In **Settings → OSC**, enable **Control by OSC and publish the
   namespace (OSCQuery)**. Keep **OSCQuery port** at `5678` unless already changed.
4. In the Open Stage Control launcher, stop the server with the square button
   before editing its options. Set these two separate fields:

   | Launcher option | File from the extracted folder |
   | :--- | :--- |
   | **load** | `/path/to/Pinol-OpenStageControl/pinol.json` |
   | **custom-module** | `/path/to/Pinol-OpenStageControl/pinol.js` |

5. Start the server with the triangle button. On the same computer, the desk
   should show **CONNECTED · 127.0.0.1:5678** and the open project's cue list.
   The **CONNECT** page changes the Pinol host and OSCQuery port; it cannot load
   a missing custom module.

The downloaded pack needs no `npm install`. Commands and feedback share the
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

## Troubleshooting

| Message or symptom | Fix |
| :--- | :--- |
| **Load pinol.js as the custom module** | Set **custom-module** to `pinol.js`, restart the server, and check the launcher log for module errors. |
| **Cannot find module 'ws'** or **'osc'** | Restore the complete pack, including `node_modules` beside `pinol.js`, or install the source dependencies below. |
| **Could not open session file**, **Unexpected token**, or **not valid JSON** | **load** must point to `pinol.json`. JavaScript belongs in **custom-module**. |
| **ECONNREFUSED** or persistent **DISCONNECTED** | Start Pinol, enable OSC / OSCQuery, and match its host and OSCQuery port. Use `5678`, not UDP `1234` or the desk's HTTP `8080`. |
| **Update Pinol: show-control API unavailable** | Use a Pinol build that includes `/show/catalog` and `/show/state`. |

To distinguish a Pinol connection problem from a launcher/module problem, open
these read-only URLs on the controller computer, adjusting the host and port:

- [Server information](http://127.0.0.1:5678/?HOST_INFO) should identify Pinol.
- [Show catalog](http://127.0.0.1:5678/show/catalog?VALUE) should return the project's catalog.

If both work but the desk remains offline, inspect the Open Stage Control
launcher log and the **custom-module** path first.

## Using a source checkout

The repository does not include `node_modules`. Install the dependencies once
inside the controller folder, using Node.js/npm:

```sh
cd /path/to/Pinol/integrations/open-stage-control
npm install --ignore-scripts --omit=optional
```

Then set **load** and **custom-module** to that folder's JSON and JavaScript
files. Repeat dependency installation if their package requirements change.

See [desk operation](../integrations/open-stage-control/README.md#operating-rules)
and the [OSC API](osc.md) for commands and feedback.
