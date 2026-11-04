# Trip Hop Drop: how it is made

The trip-hop edit of `examples/demo`, cut on its beat and rendered ahead of time under kicks, 808 basses, snares and hats made
with NyxMap's Faust DSP (`score.cpp`), and a 3D scene whose envelopes follow `cues.json` (`build_show.py`). From the repository:

```bash
clang++ -std=c++17 -O2 -Ithird_party/faust/architecture -Isrc/engine examples/trip-hop-drop/source/score.cpp -o /tmp/score
ffmpeg -y -i examples/demo/audio/*.mp3 -ar 44100 -ac 2 -f f32le /tmp/track.f32
(cd examples/trip-hop-drop/source && /tmp/score /tmp/track.f32 /tmp/mix.f32 cues.json)
ffmpeg -y -f f32le -ar 44100 -ac 2 -i /tmp/mix.f32 -c:a libmp3lame -b:a 256k "examples/trip-hop-drop/audio/Trip Hop Drop.mp3"
python3 examples/trip-hop-drop/source/build_show.py --autoplay
```
