# Vertigo Pressure

48 seconds of sub at 140 BPM: a drop that spins the floor, a wobble bass swept four then eight times a bar, a tunnel of frames
flying past the camera, and the pictures bending under it.

```bash
python3 examples/vertigo-pressure/source/build_show.py --autoplay
```

Open `Vertigo Pressure.nyxmap`: it plays at once (without `--autoplay`, GO starts it).

- **No sound file.** 14 Faust instruments run in the audio callback: a kick and a snare on the hip hop beat, a wobble bass and its
  octave, an acid FM lead whose depth is played live, a horn, a siren, a riser, air and a boom.
- **The mapping moves.** The corners of the Waveform Lines and the Spiral are pulled by swells (corner pins), the Light Bars turn on
  their rotation, the Poly Star pumps on each kick.
- **Vertigo.** The tunnel's 16 frames fly past the camera and wrap behind it; the camera's target swings in the drops; the break
  turns it half a circle. The spiral's kaleidoscope (its FX mix) opens in the second drop.
- **Each voice lights its own picture.** Its gate drives an envelope through an interaction: the kick pumps the star and the spiral's
  count, the snare the bars and the glitch on the hat lines.

The score is written by `source/build_show.py`: edit it there, then run it again.
