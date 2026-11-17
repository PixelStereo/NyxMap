# Viewports

A viewport is a window onto the composition: the region set in its **Spatial** tab, shown at its own size on a screen, a projector or a network output. Its **Spatial** tab also says how that output reaches the projector.

## Flip and flop

**Flip ↔** mirrors the output left to right (a projector behind the screen); **Flop ↕** turns it upside down (a projector hung from the ceiling). They are `flip/x` and `flip/y`, so a timeline, an interaction or OSC can set them.

## Warp

A projector rarely faces its screen square on. The **Warp** group in the Spatial tab pulls the output's picture by its four **corners** (a perspective) or by the points of a **mesh** (2 × 2 to 32 × 32, smoothly interpolated, for a curved or uneven surface). Choose **Corners** or **Mesh**, then drag the points in any view of the viewport; a move is one undo step. **Off** keeps the warp but lets nothing be dragged. **Apply** changes the mesh density (it clears the mesh), **Flatten** brings the corners and the mesh back to the whole, upright frame.

The corners are the parameters `warp/<top_left|top_right|bottom_right|bottom_left>/<x|y>` (in % of the output), so they can be animated and sent over OSC. The mesh points are saved with the project but are not part of snapshots. The flip comes first, then the warp, then the soft edge: the soft edge always fades the real sides of the output.

**✛ Cross**, on the bar above the views, draws a cross over the whole output of the viewport you point at, on the real output as well as in the view, to see where a warp point would go. It is never saved; switch it off for the show.

## Soft edge

Two projectors that overlap show the overlap twice as bright. A soft edge fades the output towards one side over a width in **pixels of the output**: set it to the overlap, on the right of the left projector and on the left of the right one. Each side has its own width and **curve**; the sides you do not blend stay at 0.

The ramp is Paul Bourke's blend, made in linear light: with the same curve on both sides, the two ramps add up to one across the overlap. It is then raised to the inverse of the projectors' **gamma** (2.2 by default; one value for the viewport), so that what the projectors light is the ramp itself. A curve of 2 is the usual start; lower is lighter, higher darker. Adjust it until the overlap shows no band.

In a view of the viewport the ramps are drawn over its picture, each with a handle on its edge: drag it to set the width (the value shows while you drag; one undo step).

| Address (under the viewport) | Value |
| --- | --- |
| `flip/x`, `flip/y` | on / off |
| `soft_edge/<left\|right\|top\|bottom>/width` | pixels of the output, 0 to none |
| `soft_edge/<left\|right\|top\|bottom>/curve` | exponent of the ramp, 0.2 to 6 (2) |
| `soft_edge/gamma` | the projectors' gamma, 1 to 3 (2.2) |

`examples/soft-edge-blending.nyxmap` is a 3520 × 1080 composition over two viewports that overlap by 320 pixels, with a test pattern.
