# Pixel mapping (DMX)

Light fixtures placed on the composition. Each pixel of a fixture is sampled from the composition at every frame (after
the composition's opacity and blackout) and streamed as DMX over Art-Net or sACN (E1.31).

Phase 1 is engine-only: there is no editor yet. A patch is written in the project file under `pixel_mapping`, or set
with `Engine::setFixturePatch`. Model: [Fixtures.h](../src/engine/Fixtures.h); network: [Dmx.h](../src/engine/Dmx.h);
GPU sampling: [PixelSampler.h](../src/engine/PixelSampler.h).

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
- A per-pixel dimmer gets the pixel's luminance (Rec. 709). A header or footer dimmer gets the fixture's intensity.
  `amber` and `uv` are sent at 0 for now.

## Fixtures

```json
{ "id": "1", "name": "Bar 1", "type": "bar16", "enabled": true,
  "x": 0.5, "y": 0.8, "w": 0.6, "h": 0.02, "rotation": 0,
  "output": "1", "universe": 1, "address": 1,
  "intensity": 1, "gamma": 1, "white": "min" }
```

- Placement: center, size and rotation (degrees, clockwise) in normalized composition coordinates, with the origin at
  the top left. The pixels are spread evenly over that rectangle and sampled at their centers.
- `universe` is the protocol's own number: the Art-Net 15-bit port address (from 0), or the sACN universe
  (1..63999). `address` is 1..512.
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

The fixture editor and the patch table, placing fixtures in the mapping view, showing them in each viewer, auto-address
and grid duplication, the DMX patch view, area sampling, Art-Net discovery (ArtPoll) and synchronization.
