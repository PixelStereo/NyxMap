# Camera Inversa

A three-minute audiovisual score for NyxMap. Seven native 3D ribs slide through
the front frame and become the walls of a cavity. A splinter field interrupts the
geometry in asymmetrical bursts. Moving shafts of light cut through the scene fog,
while a separate smoke layer drifts across the chamber.

Open **Camera Inversa.nyxmap** with the NyxMap ISF library available and the
project's `audio/` directory alongside it.

- **Reference (3:00)** is selected on open. In **Show Control** (Shift+6), press
  **GO** once. The cue clock plays all 15 cues, including the depth inversion at
  2:04, black at 2:57 and the final mix cut at 3:00.
- Select **Live GO** to send the cues by hand. The times in the cue descriptions
  are suggested positions in the score. Each scene is also available as a preset.
- **Controls** (Shift+5) gives you eleven scene faders and five independent performance
  controls. Camera angle, turbulence, grain transposition, room and the strike button
  stay under your hand across scene changes.

| Gesture | Controls |
| --- | --- |
| Bring the lip into focus | Wall reveal, Camera angle |
| Invert the chamber | Relief ↔ cavity. Move slowly through the point where the ribs align in one plane. |
| Shear and split the signal | RGB interference. The standard VHS Glitch effect adds chromatic bleed and scanline displacement. |
| Bring in the broken section | Fracture / contact noise. This raises the contact-noise bursts, reveals the splinters and brings in Pixelate at higher levels. |
| Change the physical scale | Sub level, Smoke density, Smoke turbulence. Low transients move the camera and drive the left searchlight. |
| Thin the score into flecks or stretch it into a cloud | Granular level, Grain density, Grain duration, Grain transposition, Grain space |
| Interrupt the texture | Hold and release Strike for a live FM attack. |

The sub and fracture clock loops every 21 seconds. Irregular gated noise bursts
share their timing with 3D cuts and changing mosaic sizes. The program mix drives
the searchlights and the scale of the splinters. NyxMap synthesizes the audio in
real time from FM, noise and granular voices. The bowed-metal sample is a
synthesized excitation for the granular voice.

The reusable `smoke.fs` generator lives in the standard ISF library under
`isf/generators/`. It renders self-shaded smoke on transparency and exposes density,
turbulence, rise speed, spread and tint. The chamber geometry, spotlights, fog and
camera are editable in NyxMap's native 3D scene.

The controls are also available at `/control/<key>` via NyxMap OSC. Examples include
`/control/depth`, `/control/rgb`, `/control/fracture` and `/control/strike`. Sliders
take values from 0 to 1. Send 1 on strike press and 0 on release.

Regenerate the project and audio recipes from the repository root with
`python3 examples/camera-inversa/source/build_show.py`.
