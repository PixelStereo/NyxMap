<div align="center">
  
# NYXMAP

## Pixels In New Open Logic - Pixels In New Optimal Layout
Visual Software for live visuals - From the first pixel to the final cue</p>

<p>Built for projection, installations and live performance.</p>

<p>
  <a href="https://github.com/PixelStereo/NyxMap/actions/workflows/build.yml"><img alt="Build" src="https://github.com/PixelStereo/NyxMap/actions/workflows/build.yml/badge.svg?branch=main"></a>
  <img alt="macOS · Windows · Linux" src="https://img.shields.io/badge/macOS%20%C2%B7%20Windows%20%C2%B7%20Linux-24272C?style=flat-square">
  <img alt="Qt 6 · C++17 · FFmpeg" src="https://img.shields.io/badge/Qt%206%20%C2%B7%20C%2B%2B17%20%C2%B7%20FFmpeg-24272C?style=flat-square">
</p>

<p><strong>
<a href="#built-for-the-show">Explore the features</a> &nbsp;·&nbsp;
<a href="#download">Download</a> &nbsp;·&nbsp;
<a href="docs/osc.md">OSC API</a> &nbsp;·&nbsp;
<a href="integrations/open-stage-control/README.md">Open Stage Control</a> &nbsp;·&nbsp;
<a href="docs/companion.md">Companion / Stream Deck</a> &nbsp;·&nbsp;
<a href="docs/build.md">Build and technical details</a>
</strong></p>

</div>

---

>
> NYXMAP brings video, generative visuals, spatial mapping, interactive animation of parameters and live control together in one show environment. From a single projection to a complex multi-output installation, you can design the image and control what happens next without changing tools.

## Built for the show

| | |
| :--- | :--- |
| **01 — PLAY**<br>**Your media, all together.**<br>Video, HAP, image sequences (TGA, TIFF, DPX, EXR, PNG, JPEG), images and SVG drawings, audio, text, timecode, countdown, stopwatch and clock generators, network streams (RTSP, RTMP, SRT, UDP/RTP, HLS), live inputs (cameras, screens and windows, DeckLink, NDI, OMT, Syphon and Spout), web pages and ISF generators. Layer, group, blend and combine them with effects. | **02 — MAP**<br>**Take the picture beyond the rectangle.**<br>Position, scale, rotate, corner-pin and warp with a mesh. Shape content to a surface, crop it with ROI and route it through viewports. |
| **03 — CUE**<br>**Build the moment. Then recall it.**<br>Snapshots, selective presets and sequences with waits, fades and auto-follow. Animate parameters with waveforms, Bézier keys and timelines. | **04 — CONTROL**<br>**Hands on. Eyes up.**<br>OSC, OSCQuery, MIDI, keyboard and internal interactions, with Learn, ranges, curves, filters and pick-up or jump takeover. |
| **05 — SYNC**<br>**Keep the show on time.**<br>Time-of-day and transport-based clocks, LTC, MTC and ATC, plus routing to MIDI, audio and supported video destinations. | **06 — OUTPUT**<br>**From preview to projection.**<br>Fullscreen output windows, Syphon, Spout, NDI, OMT and Blackmagic DeckLink / UltraStudio—depending on your platform and installed runtimes. |

## Made to be played live

**Build visually.** Combine videos, generators, images and effects in layers and groups. Correct color, animate parameters, and transform the composition to fit the space.

**Perform with confidence.** Store the state of a scene in a snapshot, recall only the values you want with a preset, or build a sequence of cues. Each value can have its own fade and easing.

**Connect everything.** Learn incoming OSC, MIDI or keyboard controls; shape the response with ranges, filtering and curves; or let one parameter drive another internally. OSCQuery exposes a browsable address tree for your show.

**Feed the room.** Route viewports to displays and supported video protocols. Use timecode where the show needs to stay in step with other equipment.

## Download

<!-- release-downloads:start -->
Latest published build: **[v0.4](https://github.com/PixelStereo/NyxMap/releases/tag/v0.4)**.

| macOS — Apple Silicon | Windows — 64-bit | Ubuntu — 64-bit (x86_64) |
| :---: | :---: | :---: |
| **[Download macOS](https://github.com/PixelStereo/NyxMap/releases/download/v0.4/NyxMap-macos-v0.4.zip)** | **[Download Windows](https://github.com/PixelStereo/NyxMap/releases/download/v0.4/NyxMap-windows-v0.4.zip)** | **[Download Ubuntu AppImage](https://github.com/PixelStereo/NyxMap/releases/download/v0.4/NyxMap-linux-x86_64-v0.4.AppImage)** |

Linux: Ubuntu 24.04 or newer. The **[portable archive](https://github.com/PixelStereo/NyxMap/releases/download/v0.4/NyxMap-linux-x86_64-v0.4.tar.gz)** runs without FUSE or installation. Both Linux packages include Qt, FFmpeg, NDI and OMT; see the [launch instructions](docs/build.md#get-started).

**[Download Open Stage Control remote](https://github.com/PixelStereo/NyxMap/releases/download/v0.4/NyxMap-OpenStageControl.zip)** — dependencies included; [setup guide](docs/openstagecontrol-help.md).
<!-- release-downloads:end -->

**[Open Stage Control remote](integrations/open-stage-control/README.md)** — ready to load directly from this repository, with dependencies included. See the [setup guide](docs/openstagecontrol-help.md).

To build it yourself, clone the repository and run `./build.sh` (macOS, Linux, Windows in Git Bash) or `build.cmd` (Windows): it installs what is needed, builds NyxMap and opens it. For builds between releases, download the platform artifact from a successful [GitHub Actions run](https://github.com/PixelStereo/NyxMap/actions/workflows/build.yml). See [build, installation and technical details](docs/build.md), or [limitations and remaining work](docs/README.md).

## License

NyxMap is released under the [MIT License](LICENSE). Third-party components keep their own licences: see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The NDI runtime included in the packages is covered by the NDI SDK License Agreement; NDI® is a registered trademark of Vizrt NDI AB.
