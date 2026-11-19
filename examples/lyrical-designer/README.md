# Lyrical Designer

60 seconds of one-drop dub at 72 BPM in A minor, for lines and 3D lights.

```bash
python3 examples/lyrical-designer/source/build_show.py --autoplay
```

Open `Lyrical Designer.nyxmap`: it plays at once (without `--autoplay`, GO starts it).

An original piece **inspired by** "Lyrical Designer" (Dub-Stuy, Ras Demo). Its tempo, key, bass line and chords are made up for
this example: nothing is taken from the recording, which was not available when it was written. The musical choices are the constants at
the top of `source/build_show.py` (`BPM`, the bars of each section, the chords and the two bass lines): change them and run it again.

- **No sound file.** 19 Faust instruments run in the audio callback, played by one timeline: a one-drop (the kick and the rim fall
  together on the third beat), then steppers (a kick on every beat), a skank on the offbeats with its echo opened by hand, a round
  sub in A minor, a melodica, a horn and a siren.
- **Form.** Two bars of intro, four of one-drop, two of melodica, two of dub-out (the beat gone, the bass and the snare thrown into
  the echo), four of steppers, a break (the bass alone, the horn, a snare roll), and a last hit with the bass alone in its tail.
- **Only lines and lights, no text.** A 3D tunnel of glowing lines, spots in its fog, and NyxMap's own shaders (Fil, Light Bars,
  Lines, Random Lines, Waveform Lines), found in its library rather than copied.
- **Each voice lights its own picture.** Its gate drives an envelope: the kick the cold spots and the thread's glow, the snare the
  bars and the white spots, the sub the red spots and the waveform's lines, the skank the scan lines, the hats the web.

The score is written by `source/build_show.py`: edit it there, then run it again.
