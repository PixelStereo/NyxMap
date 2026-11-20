# Pixel mapping (DMX)

Light fixtures placed on the composition. Each pixel of a fixture is sampled from the composition at every frame (after
the composition's opacity and blackout) and streamed as DMX over Art-Net or sACN (E1.31).

The fixtures live below the layers in the **Layers** panel on the left. The **Light** button shows or hides their list;
the fixture inspector is on the right, and DMX outputs are in the **Composition** tab. All of it is saved in the project under
`pixel_mapping`. Model: [Fixtures.h](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Fixtures.h); network: [Dmx.h](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Dmx.h); GPU sampling:
[PixelSampler.h](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/PixelSampler.h); GDTF: [Gdtf.h](https://github.com/PixelStereo/NyxMap/blob/main/src/engine/Gdtf.h).

## In the interface

- **Layers panel** (left): the fixture list sits directly below the layers. Use the **Light** button to show or hide it;
  fixture groups fold like layer groups. Each row has enable, lock (the fixture can still be picked but not moved), solo
  (the other fixtures send black), name (double-click to rename), intensity and its DMX address as `universe · channels`.
  Channels that overlap another fixture's on the same output show in red. **+** adds a fixture from a built-in type,
  a saved template or a GDTF import; **⧉** duplicates and **−** deletes. **STOP ALL** is available in the bottom bar
  (Cmd/Ctrl+Esc). Every edit can be undone.
- **Fixture inspector** (right):
  - type, and for a one-pixel type (Dimmer, RGB, RGBW, Dimmer RGB…) its own grid of pixels (columns × rows) with the
    pixel order (rows or columns, start corner, snake);
  - editable channel roles and fixed values, output, universe, address and the channels used;
  - center, size and rotation in composition pixels;
  - intensity, gamma and white. The same channel editor is used when creating a fixture type, importing GDTF and
    editing a fixture.
- **Composition tab**: the DMX outputs (protocol, destination, interface, rate, live state).
- **Mapping view**, with its **Fixtures** button:
  - each pixel is drawn as a cell filled with what it sends: its colour, or the grey of its level for a dimmer, with
    intensity and solo applied. The first pixel, where the DMX starts, is ringed;
  - click a fixture to select it (the list and the inspector follow). Drag it to move it, drag its square to resize
    it about its center, drag its circle to turn it (Shift: 15° steps);
  - arrow keys move it by one composition pixel (Shift: ten). Delete removes it.
- **Previews**: each view has its own **Fixtures** button, shown when the view displays the composition or a
  viewport. In a viewport, the fixtures are drawn through the viewport's frame. The choice of each view is remembered
  on this machine, not in the project.

## Fixture types

Built in (never saved): `dimmer`, `rgb`, `rgbw`, `dimmer-rgb`. A project can add its own types:

```json
{ "id": "bar16", "name": "LED Bar 16px RGB", "manufacturer": "Custom",
  "pixels": { "cols": 16, "rows": 1, "order": "rows", "start": "topLeft", "snake": false },
  "header":   [ "dimmer" ],
  "perPixel": [ "red", "green", "blue" ],
  "footer":   [ { "role": "fixed", "value": 0 } ] }
```

- Channels in DMX order: `header` (the whole fixture), then each pixel's `perPixel` channels in grid order, then `footer`.
- Roles: `dimmer`, `red`, `green`, `blue`, `white`, `amber`, `uv`, `fixed` (sends its stored value), `ignore`.
  Write `{ "role": "red", "fine": true }` for 16 bits (two channels: coarse, then fine). In the channel editor,
  choose whether a channel follows its pixel-mapping role or sends a fixed value. Add extra fixed channels after the
  fixture type's last channel; each fixed value is editable and can be automated like other show values.
- Grid order: `order` `rows` or `columns`; `start` `topLeft`, `topRight`, `bottomLeft` or `bottomRight`; `snake`
  makes every other row (or column) go back.
- A colour channel gets its pixel's colour. A channel of the whole fixture gets the average of its pixels.
  `cyan`, `magenta` and `yellow` send 1 − red, green, blue.
- A dimmer gets the fixture's intensity when its pixel (or, for a whole-fixture dimmer, the fixture) has colour
  channels. Otherwise it gets the luminance (Rec. 709). `amber` and `uv` are sent at 0 for now.
- An imported type gives its DMX map channel by channel (`dmxMap`: offset, channel, pixel) and each pixel's cell
  (`cells`), instead of `perPixel` and the grid order. It stays in its universe: channels past 512 are not sent.

## GDTF

Each DMX mode of a `.gdtf` file becomes a fixture type with the id `gdtf:<FixtureTypeID>:<mode>`. The type is copied
into the project, so the file is not needed afterwards.

- Kept: intensity and colour attributes (`Dimmer`, `ColorAdd_R/G/B/W/WW/CW/A/UV`, `ColorRGB_*`, `ColorSub_C/M/Y`), in
  8 or 16 bits. Every other channel (pan, tilt, gobo, strobe…) sends its default value.
- Pixels: the geometry references of the mode's geometry. A template channel is repeated for each reference, at its
  break's offset. The pixels are laid out in a grid from their positions. Without references, each geometry that
  carries colour channels is a pixel; with only one, the fixture is a single pixel.
- Channel assignments can be reviewed and changed in the channel editor while importing; the same editor is available
  when creating a fixture and later in its inspector. Only DMX break 1 is patched; channels on other breaks are reported
  and left out.
- The zip reader is built in (stored and deflated entries, no zip64).

## Fixture type templates

Use **New Fixture Type** to create a type without a GDTF file: define its pixel grid, whole-fixture header/footer
channels and per-pixel channels. Choose each channel's role or a fixed value. Save the type as a template from the
fixture inspector; templates are kept on this machine and offered when creating fixtures in any project. A fixture
created from a template copies the type into the project, so the template can later be changed or removed without
changing existing projects. Templates are local to the machine; they are not automatically shared with a project.

## Fixtures

```json
{ "id": "1", "name": "Strip 1", "type": "rgb", "enabled": true, "locked": false, "solo": false,
  "pixels": { "cols": 16, "rows": 1, "order": "rows", "start": "topLeft", "snake": false },
  "x": 0.5, "y": 0.8, "w": 0.6, "h": 0.02, "rotation": 0,
  "output": "1", "universe": 0, "address": 1,
  "intensity": 1, "gamma": 1, "white": "min" }
```

- Placement: center, size and rotation (degrees, clockwise) in normalized composition coordinates, with the origin at
  the top left. The pixels are spread evenly over that rectangle and sampled at their centers.
- `pixels`: a fixture of a one-pixel type gets its own grid. The type's per-pixel channels repeat for each pixel, and
  its header and footer stay once. The pixel order is set here. A type that has its own pixels ignores this.
- `universe` is the protocol's own number: the Art-Net 15-bit port address (from 0, the default), or the sACN universe
  (1..63999; a fixture on an sACN output starts at 1). `address` is 1..512.
- `locked` keeps it in place in the views. `solo`: while any fixture is soloed, the others send black (their fixed
  channels keep their values).
- A pixel never straddles two universes: a pixel that would cross channel 512 starts at channel 1 of the next universe.
- `gamma` applies to the sampled colour and `intensity` to every value. `white`: `min` moves the grey part of the
  colour (min of R, G, B) to the white channel; `off` leaves white at 0.
- `path` (a strip): its pixels are spread evenly along a polyline instead of a rectangle, in DMX order from its first
  point. **Fixtures on Lines** (a Line generator's Source tab, see `parameter-control.md`) makes one on each line of the
  layer, linked to it (`path.line_layer`, `path.line`), and the engine keeps its path where that line is on the
  composition.

### In snapshots, OSC, interactions and timelines

Fixture and group controls are show values. Fixture paths include `fixture/<id>/enable`, `fixture/<id>/solo`,
`fixture/<id>/intensity`, and `fixture/<id>/chN` for a fixed channel. Group paths use `fixture/g<id>/enable`,
`fixture/g<id>/solo` and `fixture/g<id>/intensity`. They can be driven by snapshots, presets, timelines, interactions
and operator controls; right-click a value in the list or inspector to add it to those systems. A fixture's fixed-channel
values are stored with its captured fixture values. A snapshot stores the show values for each fixture. The Snapshots
inspector lists them under **Fixtures**, where each fixture can be left out. A recall sets on/off and solo at once and
fades intensity and fixed-channel values according to the snapshot's transition and per-value delays. A fixture made
after the snapshot, or left out of it, is left alone. One removed since is not made again: the patch itself (types,
placement, addresses, outputs) is not part of a snapshot. Group values can be captured and recalled independently.

## DMX outputs

In the Composition tab: a list of the outputs (on, name, protocol, state), and the settings of the one selected. Only
the settings that apply to its protocol and mode are shown.

```json
{ "id": "1", "name": "Nodes", "protocol": "artnet", "enabled": true,
  "delivery": "unicast", "destinations": ["2.0.0.10", "2.0.0.11"], "interface": "192.168.1.20", "rate": 40,
  "transmission": "full" }
```

The outputs are [pxs](https://github.com/PixelStereo/pxs)'s (`pxs::dmxout`), the same in every Pixel Stereo application that
sends DMX. Its settings for a universe map (`firstSourceUniverse`, `firstUniverse`, `universeCount`) are saved but not used
here: a fixture addresses its output and its network universe itself.

- `protocol`: `artnet` (UDP 6454) or `sacn` (UDP 5568). `port` overrides the port (the panel's *Port*, "Protocol default" at 0).
- `interface`: the local address to send from, chosen in a menu of this machine's network cards (empty: the system's
  choice). It picks the card for broadcast and multicast too.
- `delivery`: `group` is the protocol's own: Art-Net broadcasts to the card's network, sACN sends each universe to its
  multicast group `239.255.hi.lo`. `unicast` sends to one or more IP addresses (`destinations`, typed separated by commas); the
  addresses only show in unicast.
- sACN only: `priority` (0..200, default 100) and `sourceName`. NyxMap also sends per-address priority data (start code
  `0xDD`): channels owned by a fixture receive the output priority; unpatched and `ignore` channels receive priority 0, so
  another source can own them. The CID is kept in the app settings, so it stays the same on this machine. When an output is
  removed or disabled, or NyxMap quits, its sACN streams are terminated (three frames).
- `rate`: packets per second for each universe (1..44 for Art-Net, the protocol's limit; 1..120 for sACN; default 40). All of
  an output's universes are sent at that rate, on a thread of its own, whatever the render's frame rate.
- `transmission`: `full` sends 512 channels in every packet; `partial` sends each universe only up to its last channel a
  fixture uses, for receivers that take a short frame (less traffic).
- An address that is not an IPv4 address, or unicast without any address, puts the output in error (shown in its state).

## Not yet done

Still not implemented: grid duplication, the DMX patch view (512 cells per universe), area sampling, GDTF multi-break
fixtures, MVR import, Art-Net discovery (ArtPoll) and synchronization.
