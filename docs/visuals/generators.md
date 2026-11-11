# Generators

Built-in generators of text, time and lines. Their settings are parameters (`param/…`): animate them, drive them with interactions, recall them in snapshots and presets, or set them over [OSC](../reference/osc.md).

## Cursive: text written as by a pen

The Media Bin lists **Cursive** under Generators: a text layer in a cursive font of the computer (Snell Roundhand, Segoe Script…), with all that a text has (any font, size, color, outline, shadow, alignment). Any text layer can be written the same way, from its **Writing** box: **▶ Write**, the pause, **Clear**, **All**, how much is written and in what time.

| Address | Meaning |
| :--- | :--- |
| `text/write/progress` | How much is written, `0…1`. At 1 it is a text like any other. A timeline, an interaction, OSC or a snapshot's fade can drive it. |
| `text/write/duration` | Seconds **Write** takes. |
| `text/write/start`, `pause`, `resume`, `reset` | Its transport: from the start, hold, go on, nothing written. |

No special font is needed. The letters are thinned down to their skeleton, the pen goes along it at an even pace, one piece of ink after the other in the order of the reading (a dot or an accent after its word), and each pixel, outline and shadow included, shows when the pen passes nearest. A font does not say how its letters are drawn, so this order is worked out from their shapes: at a crossing it can differ from a real hand, and it looks most natural with a connected script font. **STOP ALL** holds the pen where it is. The picture is worked out once per text and style, and sent again at each frame while it is being written: keep the layer's Width and Height near what the text needs.

## Countdown, chronometer and clock

The Media Bin lists **Countdown**, **Chronometer** and **Clock** under Generators, with Text and Timecode. Dropped onto a layer, each one is a text layer (same font, size, color, alignment, outline and shadow controls as Text) that shows a time. Their settings and transport are layer parameters under `clock/`: OSC addresses (`/layer/<name>/clock/...`), interactions, snapshots and presets reach them like any other value.

| Address | Generators | Meaning |
|---|---|---|
| `clock/target` | Countdown | `duration` or `time_of_day` (counts down to the next occurrence of `clock/target_time`, across midnight if needed). |
| `clock/duration` | Countdown | Seconds it counts down from. |
| `clock/target_time` | Countdown | Time of day, as text (`20:30:00`, `8:30 PM`). |
| `clock/offset` | Chronometer | Seconds it counts up from. |
| `clock/format`, `clock/custom_format` | All | Countdown and chronometer: `hh:mm:ss`, `mm:ss`, `ss`, with tenths (`.t`) or frames (`:ff`), or `custom` with tokens `h hh m mm s ss t tt ttt ff` and `'quoted text'`. Clock: `24h`, `12h`, or `custom` with a Qt date-time format. |
| `clock/fps` | Countdown, Chronometer | Frames per second for the `ff` token. |
| `clock/at_zero`, `clock/zero_text`, `clock/overtime_color/r,g,b,a` | Countdown | At zero: `stop` (holds 00:00), `text` (shows the text), or `overtime` (counts on below zero in the overtime color). |
| `clock/seconds`, `clock/date`, `clock/zone` | Clock | Seconds and date shown; time zone (empty: system, `UTC`, `+02:00`, `Europe/Paris`). `clock/zone` also applies to a countdown to a time of day. |
| `clock/play` | Countdown, Chronometer | Running or paused (stored in snapshots and presets). |
| `clock/elapsed` | Countdown, Chronometer | Seconds run since the start (stored; a recall leaves a clock that is already running untouched). |
| `clock/start`, `clock/pause`, `clock/reset` | Countdown, Chronometer | Actions. Reset returns to the start and pauses; starting a countdown that finished starts it again from its full duration. |
| `clock/restart` | Countdown, Chronometer | "Restart on recall": a snapshot or preset holding it on starts the clock from the beginning. |
| `clock/value`, `clock/zero` | Read only | The time shown in seconds; whether a countdown reached zero. An internal interaction can follow `clock/zero` to drive another parameter at zero. |

Countdowns and chronometers follow real time (not the composition speed). Only their own transport and **STOP ALL** pause them. A project saves the elapsed time and running state; on load, a running clock continues from the saved value.

## Line

The Media Bin also lists **Line** under Generators. It works like MadMapper's Line tool: you draw segments on a layer, then animate them along their length. Its shader is built into the application, so a project never loses it. The layer's source is saved as `/NyxMap/Generators/Line` with its lines, and there is no file to find, relink or collect.

**Drawing.** Dropping Line onto a layer starts drawing a line in the preview. **Draw Line** in its Source tab draws another; a layer holds as many lines as needed.
- Click each point. Enter, a double-click, or the first point again ends the line (the first point again closes it).
- Backspace takes the last point back. Esc cancels.
- Afterwards: drag a point to move it, drag a segment to move the whole line, double-click a segment to add a point. Delete removes the selected point (or the line, below two points) or the selected line.
- Every edit is undoable. **Delete All** removes them all.

The points are in the layer's own frame, so moving, scaling or warping the layer moves its lines with it.

**Animation.** These are the shader's inputs, layer parameters under `param/` (`/layer/<name>/param/rate`…), so they can be animated and reached from OSC, interactions, snapshots and presets. The Source tab's `speed` scales its time; Resolution sets its picture size, as for any ISF generator.

| Address | Meaning |
|---|---|
| `param/animation` | `0` Solid, `1` Fill, `2` Chase, `3` Strobe, `4` Pulse, `5` Media. |
| `param/lineColor/r,g,b,a` | The lines' color. With `param/gradient` on, it goes to `param/endColor` from each line's start to its end. |
| `param/backColor/r,g,b,a` | Off the lines and where they are not lit (transparent by default). |
| `param/thickness` | Pixels of the generator's picture. Joins and ends are round. `param/softness` feathers the edges. |
| `param/rate` | Cycles per second, either way: the fill's loop, the dashes' scrolling, the strobe's and the pulse's frequency, the media's scrolling. |
| `param/fill` | Fill with `rate` 0: the part lit from the start (animate it to grow the line). With a rate, the fill loops. |
| `param/dashes`, `param/dashWidth` | Chase: dashes along each line, each one a fraction of its period. |
| `param/duty` | Strobe: the part of each cycle lit. |
| `param/along` | `0` each line from its start to its end, `1` each segment on its own. |
| `param/reverse` | From the end to the start. |
| `param/cascade`, `param/cascadeBy` | Offset: each line (`0`) or each segment of a line (`1`) starts this fraction of a cycle after the one before it. |
| `param/media` | Media: an image or another layer's picture laid along the lines, its width along the path, its height across the thickness. |

**Pixel mapping.** **Fixtures on Lines** in the Source tab puts a fixture of the chosen one-pixel type (RGB, RGBW…) on each line. Each one has that many pixels spread evenly along its line, in DMX order from the line's first point, at the next free addresses. It follows its line when the line is edited or the layer moves. In the views it shows as a strip: its path and its pixels. It cannot be moved, resized or turned on its own. A fixture keeps its last place if its layer or line is removed. A layer inside a group is followed through its own mapping only, not its group's.
