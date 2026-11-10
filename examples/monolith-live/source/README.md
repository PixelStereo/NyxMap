# Monolith Live

A 67.5-second show with live Faust drums and drones, a trip-hop tape edit, and a waveform thread over the sea and the monolith.

Open **Monolith Live.nyxmap** and press **GO** on **Start / rewind**. The internal **Show Timecode** runs from zero. The sequence recalls each picture preset at its timecode, including the second tape at 00:30.77. To replay, trigger **Start / rewind** again.

- **Presets** hold the image cuts, window geometry, crops, FX switches and camera framing, plus discrete chord and mixer changes. Each picture preset includes the windows it needs, so a timecode jump can recall the landed shot directly.
- **Instrument score** plays the notes and drum gates. **Mix and filter sweeps** carries the ramps, risers and tape chops. **Thread, camera and 3D motion** carries the continuous movement and the final fade.
- The picture still responds to hits in the audio mix. Seeking the timecode recalls a picture preset. Start from zero for synchronized playback of the tape and timelines.

Run `python3 -B examples/monolith-live/source/build_show.py` to regenerate the project. Add `--autoplay` to make it start on open.
