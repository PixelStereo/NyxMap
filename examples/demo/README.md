# Demo

A 30-second audiovisual piece played by its soundtrack: a cue list on the soundtrack's timecode, scenes, presets,
timelines and interactions that listen to the music, in black and white with colour breaking in.

```bash
python3 examples/demo/build_show.py --audio track.mp3 --out ~/Shows/Demo
```

The soundtrack is not in the repository. The cues are written for a 30-second trip-hop edit at 78 BPM (a beat every
0.769 s); any track plays them, on that beat. The show folder gets `Demo.nyxmap`, its shaders (copied from NyxMap's ISF
library) and the soundtrack.

Open the project: the sequence stands by on its first step. **GO** starts the soundtrack, and from there the
soundtrack's playhead plays every other step at its timecode. Seek the soundtrack and the show follows: the last step
before the playhead plays. `--autoplay` makes the project GO by itself when it opens.

To watch it without the interface, as a video with its sound:

```bash
NyxMap --render frames/f-####.jpg --play --seconds 30.5 --every 0.04 ~/Shows/Demo/Demo.nyxmap
ffmpeg -framerate 25 -i frames/f-%04d.jpg -i track.mp3 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest demo.mp4
```

## What it uses

- **A sequence on a timecode.** The sequence follows the Soundtrack layer's Playhead. Twenty-two steps have a timecode,
  placed on the beat. The other steps follow them at once: a timeline played or stopped with its scene, the scene after
  a flash.
- **One snapshot, then presets.** The first step is the show's only snapshot: every layer set, the soundtrack
  started. Every other scene is a preset of what changes from the scene before it, plus the values something else moved
  since that scene (the presets played in between, the timelines playing, the interactions it had switched on), so that
  it leaves exactly what a full snapshot would. Some values have their own time and easing: the readout's text is
  typed in, a flash fades out while the rest cuts. The interactions are switched on and off by their `enable` value.
- **Presets** for the fastest moves:
  - **Flash** turns one bar full width and full white, for 0.1 s before the next scene;
  - **Cut to black** makes a hole in the music.
- **Timelines:**
  - **Rise** keyframes the data band's height;
  - **Drift** turns the lattice camera with oscillators;
  - **Reframing** gives the three windows a new place, crop (ROI), rotation and Soft Crop wipe on each beat, as hold keys;
  - **Sweep** turns the bars by 45° per beat;
  - **Spin** rotates the lattice layer a full turn every two bars;
  - **Title tilt** pulls the title's corners back into depth;
  - **Corridor** swings the vanishing point of three corner-pinned planes;
  - **Fade out** removes the layers one by one.
  A sequence step doubles the speed of Reframing for the finale.
- **Interactions.** The scenes switch them on and off.
  - **Audio bands:**
    - the kick, through pulse detection, flashes the bars, the lattice and the waves, and toggles the data's inversion;
    - the mids raise the waves;
    - the highs make the data flicker;
    - the level makes the lattice glow.
  - **Action:** a kick sets the lattice's field of view and cell, the bars' jitter and the waves' thickness at once.
    When the kick lets go, each value goes back over its own time and easing.
  - **Action:** a burst in the highs tears the data (VHS glitch) at once, then heals it over 0.35 s.
  - **Action on the playhead:** past the last note the show goes to black; rewound, it comes back.
  - **Internal:** the soundtrack's progress draws the data's bottom line and slowly rolls the lattice.
- **Layers:**
  - four generators from the library, written for this piece:
    - Data Stream (barcodes, matrix, spectrum bars from the program's FFT);
    - Beam Lattice (a ray-marched lattice to fly through);
    - Light Bars;
    - Waveform Lines (the program's waveform, line after line, receding);
  - a Linear Gradient as a colour wash in **multiply**;
  - **Layer** sources: three windows reframe the lattice and the data;
  - a huge **text** whose letters are the **matte** of a copy of the waves, so there are waves inside the letters and
    the amber lattice around them;
  - a Timecode generator;
  - a typed readout.
- **Blend modes:** difference (the data over the lattice), add (bars, waves), screen (inside the title), multiply (the
  washes).
- **Effects:** Soft Crop, Kaleidoscope, Trails (the tunnel), Zoom Volumetric, VHS Glitch.
