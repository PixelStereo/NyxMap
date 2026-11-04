# Monolith Live

Rhythms and drones synthesized by NyxMap as it plays, a picture that changes at every beat, and one thread through it all.
After the sea and the monolith of [Renaud Rubiano](https://rubiano.fr).

```bash
python3 examples/monolith-live/source/build_show.py --autoplay
```

Open `Monolith Live.nyxmap`: it plays at once, 67 s (without `--autoplay`, GO starts it).

- **The sound runs in the audio callback.** 17 Faust instruments, each with its envelope and its Audio FX: kick (3 voices),
  snare (2), hats, toms and rims in threes and fives against the four, bursts that speed up and slow down, an 808, five
  drones that beat against each other while their filters open and close. The trip-hop edit of `examples/demo` plays
  through its own filters, delay and reverb under them. One timeline plays it all: its notes fall on frames, so at 60 fps a
  note can be up to 16 ms late.
- **The pictures listen.** Hits heard in the program mix (bass, mids, highs) become 27 envelopes on sizes, deformation,
  lights and glow.
- **They change at every beat.** 79 images: 14 windows that crop eight sources (a sea written for this piece, the 3D stage,
  shaders) in columns, rows, slits, grids, nested and in scales in all directions, seen through Kaleidoscope, Zoom
  Volumetric, Pixelate, VHS Glitch, FlipFlop or Trails.
- **The 3D stage:** the monolith in a sea displaced by noise, rings and stones of noise, white lines and points, beams in
  fog, the camera cut at each bar.
- **The thread:** one white line drawn from the waveform, on top of everything, from the first seconds to the last. It
  grows, splits into forty in the break, and comes back to one.
