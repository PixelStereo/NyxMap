# Viewports

The **Viewports** tab, between Inspector and Composition, lists every viewport: **+** adds one, **−** deletes the selected ones, a double-click renames, and several can be selected to edit or paste onto at once. Under the list are the parameters of the one picked.

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

### Lining projectors up

Four tools, from what Resolume, Watchout, Epson and VIOSO recommend:

- **Guide ▸ Pattern** (or **▦ Guide** above the views, for every viewport at once) replaces the output with a pattern: the overlap of each blended side in the viewport's **color**, a fine grid with a stronger line every 128 px, the diagonals, and a white line at the end of each ramp. Give neighbours colors that complete each other (red and cyan, the default order): where the projectors line up, the two overlaps make one white band and the grid runs straight through both. A fringe of one color is a misalignment. **Overlap only** blacks out everything else, to set one projector at a time; **Ramp** shows the pattern through the fade, as it is thrown (off: the pattern whole).
- **Guide ▸ White / Grey / Black** are uniform fields. On white, adjust the **Curve** and **Gamma** until the overlap has the same brightness as the rest, with no dark or bright band (match the projectors' white point first).
- **Black** lifts the black outside the overlap. A projector never shows a true black, so the overlap, lit by two, has a black twice as bright and shows as a grey band on black. On the Black field, raise it (in % of white) until the band goes. It costs contrast outside the overlap: the better fix is a physical mask on the projectors, or better projectors.
- The overlap should be at least 10 to 20 % of the width: below 5 % a blend is hard to hide.

The guide is never saved and shows on the real outputs: switch it off for the show.

In a view of the viewport the ramps are drawn over its picture, each with a handle on its edge: drag it to set the width (the value shows while you drag; one undo step).

| Address (under the viewport) | Value |
| --- | --- |
| `flip/x`, `flip/y` | on / off |
| `soft_edge/<left\|right\|top\|bottom>/width` | pixels of the output, 0 to none |
| `soft_edge/<left\|right\|top\|bottom>/curve` | exponent of the ramp, 0.2 to 6 (2) |
| `soft_edge/gamma` | the projectors' gamma, 1 to 3 (2.2) |
| `soft_edge/black` | black level lifted outside the overlap, 0 to 30 % of white |
| `soft_edge/guide/<r\|g\|b>` | the viewport's color in the guide |

`examples/soft-edge-blending.nyxmap` is a 3520 × 1080 composition over two viewports that overlap by 320 pixels, with a test pattern.

## Channels

Every view (the mapping view of the Composition and the previews of layers and viewports) has an **RGB ▾** menu in its bar: **R**, **G**, **B** and **A** show that channel in black and white (A: the alpha, white where opaque). It is done by the view's own shader on the picture it already has: the render and the outputs are untouched, and it costs nothing. The choice is kept with the view.
