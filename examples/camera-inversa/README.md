# Camera Inversa

A three-minute audiovisual score for NyxMap. An apparent solid projects into the room,
then turns into a cavity around the same lip. The smoke is a procedural volume with
self-shading and extinction. The image is monochrome until the RGB memory brings back
two older frames in separate channels.

Open **Camera Inversa.nyxmap** with its shader and `audio/` directory alongside it.

- **Live GO** is selected on open. In **Show Control** (Shift+6), press **GO** for
  *First breath*. Send the following cues whenever you want, or click them in the cue
  stack. The times in the cue descriptions are suggested positions in the score.
  Each scene is also available as a preset.
- **3:00** runs the score from a single **GO**. Its group recalls the same 15 cues
  on the cue clock, including the depth inversion at 2:04, black at 2:57 and the
  final mix cut at 3:00.
- **Controls** (Shift+5) gives you ten scene faders and five independent performance
  controls. Every cue recalls a starting mix and the faders remain playable between
  cues. Camera angle, turbulence, grain transposition, room and the strike button
  stay under your hand across scene changes.

| Gesture | Controls |
| --- | --- |
| Bring the lip into focus | Wall reveal, Camera angle |
| Invert the trompe-l’œil by hand | Relief ↔ cavity. Move across the middle slowly to flatten the walls before they recede. |
| Keep the frame monochrome or tear it | RGB memory. The mix's upper transients push the separation when the fader is up. |
| Change the physical scale | Sub level, Smoke density, Smoke turbulence. Low transients move the camera toward the volume. |
| Thin the score into flecks or stretch it into a cloud | Granular level, Grain density, Grain duration, Grain transposition, Grain space |
| Interrupt the texture | Hold and release Strike for a live FM attack. |

The sub phrase loops every 21 seconds with an uneven distribution of gates and
pitches. NyxMap generates the air, sub, strikes and grains in real time. The bundled
bowed-metal sample is a synthesized excitation for the granular voice. There is no
rendered soundtrack. The 3D image and the smoke are raymarched in a two-pass ISF
generator at 960×540 and mapped to a 1280×720 composition.

The controls are also available at `/control/<key>` via NyxMap OSC. Examples include
`/control/depth`, `/control/rgb` and `/control/strike`. Sliders take values from 0
to 1. Send 1 on strike press and 0 on release.

Regenerate the project and audio recipes from the repository root with
`python3 examples/camera-inversa/source/build_show.py`.
