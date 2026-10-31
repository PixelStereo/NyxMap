# Layer audio FX

Select a sound layer, generated audio layer, or video layer with an audio track.
In **Source → Audio FX**, choose **Add Audio FX…**. The chain runs from top to
bottom, before the layer's volume, mute and visibility fader. Each effect has
**Enabled**, **Dry/Wet**, reorder arrows, Remove and live controls. Structural
edits and parameter changes support undo/redo.

Available effects use GRAME Faust's standard DSP libraries:

| Effect | Main controls |
| --- | --- |
| Low-pass / High-pass / Band-pass | Cutoff and resonance |
| 3-band EQ | Bass, mid and treble gain in dB |
| Delay | Time in ms, feedback |
| Freeverb | Room and damping |
| Distortion | Drive and tone |
| Compressor | Threshold, ratio, attack, release, makeup gain |
| Noise gate | Threshold, attack, hold, release |
| Chorus / Flanger | Rate, depth, flanger feedback |
| Tremolo | Rate and depth |
| Stereo width | 0 = mono, 1 = original stereo, 2 = wider |

Delay/reverb/chorus/flanger start with a partial wet mix. Filters, EQ and dynamics
start fully wet. Bypass and wet changes are smoothed to reduce clicks; bypassed
processors continue running so their state is retained. Pausing a source permits
effect tails to decay. Muting, hiding, or fading the layer silences the entire
processed signal, including tails. Effects survive source replacement.

The chain is limited to 16 effects per layer. Programs are prepared outside the
callback; processing uses fixed scratch buffers and immutable chain publication.
Adding, removing or moving an effect does not compile or allocate in the audio
callback. Reordering retained effect IDs preserves DSP state. Chains and controls
are saved in project/layer state. Missing custom FX are preserved with their saved
values, shown with an error and passed through dry until restored.

Controls use the normal parameter registry: `audio_fx/<effect-id>/enabled`,
`audio_fx/<effect-id>/wet` and `audio_fx/<effect-id>/p0`, `p1`, etc. Their labels
identify the actual Faust controls. They are available to OSC, interactions,
timelines and snapshots while the corresponding effect exists. Snapshot values do
not recreate a removed effect; restore the chain through project/layer state or undo.

## Custom Faust effects

With the optional libfaust compiler enabled, **Load Faust stereo FX (.dsp)…** loads
a program with exactly **two audio inputs and two outputs**. For example:

```faust
import("stdfaust.lib");
cutoff = hslider("Cutoff", 2000, 20, 20000, 1) : si.smoo;
filter = fi.lowpass(2, cutoff);
process = filter, filter;
```

Scalar controls are discovered automatically (maximum 128); soundfile widgets are
not supported. Invalid programs leave the current chain unchanged. Program files
use project-relative paths when saved and are discovered by Collect Files; Faust
standard libraries and additional imports still require the configured Faust
runtime/library installation. Built-in FX require no external Faust installation.

DSP source lives in `audio/fx`, with generated C++ in `src/engine/generated`.
`scripts/generate_audio_dsp.sh` regenerates instruments and effects with the
pinned Faust/compiler libraries. The same generated C++ supports macOS, Windows
and Linux through the existing miniaudio output. Cloud validation uses Linux and
null audio devices; native device latency and macOS/Windows playback still need
platform testing. Master/group buses and multichannel effects are not implemented.
