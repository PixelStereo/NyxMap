# Pixel mapping (DMX)

Light fixtures placed on the composition. Each pixel of a fixture is sampled from the composition at every frame (after
the composition's opacity and blackout) and streamed as DMX over Art-Net or sACN (E1.31).

The fixtures are listed in the **Fixtures** tab on the left (next to Layers), their settings are in the **Fixture**
inspector on the right, and the DMX outputs are in the **Composition** tab. All of it is saved in the project under
`pixel_mapping`. Model: [Fixtures.h](../src/engine/Fixtures.h); network: [Dmx.h](../src/engine/Dmx.h); GPU sampling:
[PixelSampler.h](../src/engine/PixelSampler.h); GDTF: [Gdtf.h](../src/engine/Gdtf.h).

## In the interface

- **Fixtures list** (left), like the layers:
  - each row has enable, lock (the fixture can still be picked but not moved), solo (the other fixtures send black),
    name (double-click to rename), an intensity slider (a factor on everything the fixture sends) and its DMX address
    as `universe · channels`;
  - channels that overlap another fixture's on the same output show in red;
  - **+** adds a fixture of a chosen type at the next free address (creating an Art-Net output if there is none),
    imports GDTF files, or removes unused types. **⧉** duplicates, **−** deletes. Every edit can be undone.
- **Fixture inspector** (right):
  - type, and for a one-pixel type (Dimmer, RGB, RGBW, Dimmer RGB…) its own grid of pixels (columns × rows) with the
    pixel order (rows or columns, start corner, snake);
  - output, universe, address and the channels used;
  - center, size and rotation in composition pixels;
  - intensity, gamma and white.
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
- Roles: `dimmer`, `red`, `green`, `blue`, `white`, `amber`, `uv`, `fixed` (sends `value`), `ignore`. Write
  `{ "role": "red", "fine": true }` for 16 bits (two channels: coarse, then fine).
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
- Only DMX break 1 is patched; channels on other breaks are reported and left out.
- The zip reader is built in (stored and deflated entries, no zip64).

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

## DMX outputs

```json
{ "id": "1", "name": "Nodes", "protocol": "artnet", "enabled": true,
  "destination": "", "interface": "", "rate": 40 }
```

- `protocol`: `artnet` (UDP 6454) or `sacn` (UDP 5568). `port` overrides the port.
- `destination`: an IP address for unicast. When empty, Art-Net broadcasts (on the interface's broadcast address if
  `interface` is set) and sACN uses the multicast group `239.255.hi.lo` of each universe.
- `interface`: the local address to send from, which selects the network card.
- `rate`: packets per second for each universe (1..120, default 40). All of an output's universes are sent at that
  rate, whatever the render's frame rate.
- sACN only: `priority` (0..200, default 100) and `source_name`. The CID is kept in the app settings, so it stays the
  same on this machine. When an output is removed or disabled, or Pinol quits, its sACN streams are terminated.

## Not yet done

An editor for custom fixture types (they can be written in the project's JSON for now), grid duplication, the DMX patch
view (512 cells per universe), area sampling, GDTF multi-break fixtures, MVR import, Art-Net discovery (ArtPoll) and
synchronization.
