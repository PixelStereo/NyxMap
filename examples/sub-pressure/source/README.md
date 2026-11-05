# Sub Pressure

30 seconds of slow UK dub at 82 BPM, between hip hop and ragga, for pulses of lines and 3D lights.

```bash
python3 examples/sub-pressure/source/build_show.py --autoplay
```

Open `Sub Pressure.nyxmap`: it plays at once (without `--autoplay`, GO starts it).

- **No sound file.** 19 Faust instruments run in the audio callback, played by one timeline: a swung hip hop beat, a dembow on
  the rim, a skank on the offbeats with its echo opened by hand, a walking sub in E minor, a melodica, a siren and a horn.
- **Only lines and lights.** A 3D tunnel of glowing lines, spots in its fog, and NyxMap's own shaders (Fil, Light Bars, Lines,
  Random Lines, Waveform Lines), found in its library rather than copied.
- **Each voice lights its own picture.** Its gate drives an envelope: the kick the cold spots and the thread's glow, the snare the
  bars and the white spots, the sub the red spots and the waveform's lines, the skank the scan lines, the hats the web.
  Pulses also run down the tunnel on the kick and back up it on the snare.
