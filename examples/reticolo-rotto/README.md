# Reticolo Rotto

48 seconds of break bass at 160 BPM, scored for NyxMap's **Beam Lattice.fs**. The soundtrack is synthesized in real time by one stereo Faust instrument. Its notes, rolls and fills are editable in the NyxMap timeline.

Open **Reticolo Rotto.nyxmap** and hit **GO** in **Reference** for the full score. Switch to **Live GO** to play any of the seven cues on its own.

| Control | Gesture |
| --- | --- |
| Impact | Turn up the response to low transients. |
| Crop | Close in on the center of the source. |
| Multipli | Pack more beams into the lattice. |
| FX Mapping | Pull the corner pins into an asymmetrical projection. |
| Bloom, Twist, Edge | Shape the snare glow, depth twist and metallic transients. |
| Kick, Bass, Break | Balance the three parts of the synth mix. |
| Bass Drive, Width | Push the bass harmonics and spread the upper frequencies. |

The first four controls pick up a new starting value at each cue. The others stay where you leave them.

Keep the `audio` folder next to the project and open it in a NyxMap build with Faust compilation enabled. The shader comes from NyxMap's standard library. Run `python3 -B source/build_show.py` to rebuild the project after editing the score.
