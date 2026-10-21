# Third-party notices

Pinol's own code is under the MIT licence (see LICENSE). It uses the following third-party components, each under its
own licence; their full texts are in the `licenses` folder (and, for those fetched at packaging time, next to the
library).

| Component | Use in Pinol | Licence | Licence text | Source |
|---|---|---|---|---|
| Qt 6 | User interface, OpenGL, multimedia | GNU LGPL v3 (dynamically linked) | `licenses/LGPL-3.0.txt`, `licenses/GPL-3.0.txt` | https://www.qt.io/download-open-source |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample…) | Video and audio decoding | GNU LGPL v2.1 or later (dynamically linked; built without GPL components) | `licenses/LGPL-2.1.txt` | https://ffmpeg.org/download.html |
| miniaudio | Audio input and output | Public domain (Unlicense) or MIT No Attribution | `licenses/miniaudio.txt` | https://github.com/mackron/miniaudio |
| RtMidi | MIDI | MIT-style (RtMidi licence) | `licenses/rtmidi.txt` | https://github.com/thestk/rtmidi |
| bcdec | BC texture decoding (HAP) | MIT or Unlicense | `licenses/bcdec.txt` | https://github.com/iOrange/bcdec |
| Syphon Framework (macOS) | Syphon input and output | BSD 3-clause | `licenses/syphon.txt` | https://github.com/Syphon/Syphon-Framework |
| Spout2 (Windows) | Spout input and output | BSD 2-clause | `licenses/spout2.txt` | https://github.com/leadedge/Spout2 |
| Blackmagic DeckLink SDK headers | DeckLink output (the driver is installed separately) | Boost Software License 1.0 | `licenses/decklink.txt` | https://www.blackmagicdesign.com/developer |
| Open Media Transport: libomtnet, libomt, libvmx | OMT input and output | MIT | `OMT-LICENSES.txt` (in the package) | https://github.com/openmediatransport |
| NDI® runtime | NDI input and output | NDI SDK License Agreement | `NDI-NOTICE.txt`, `Processing.NDI.Lib.Licenses.txt` (in the package) | https://ndi.video |
| AppImage runtime (Linux) | Portable executable launcher | MIT; linked libfuse under LGPL v2.1, other linked libraries under permissive licences | `licenses/appimage-runtime.txt`, `licenses/LGPL-2.1.txt` | https://github.com/AppImage/type2-runtime |

NDI® is a registered trademark of Vizrt NDI AB. Information about NDI and its tools: https://ndi.video

**NDI runtime in the packages.** The NDI runtime is not covered by Pinol's MIT licence: it is provided by Vizrt NDI AB
under the NDI SDK License Agreement. You may not modify, disassemble, decompile or separately distribute the NDI
components, nor use them outside Pinol. Vizrt NDI AB and its licensors provide them "as is", without warranty of any
kind, and disclaim all liability for them to the extent permitted by law.

**LGPL components (Qt, FFmpeg).** They are dynamically linked: you may replace the libraries shipped with Pinol by
compatible modified versions (in `Pinol.app/Contents/Frameworks` on macOS, next to `Pinol.exe` on Windows). Their
corresponding source code is available from the links above; the exact versions used by a package are listed in its
`DEPENDENCIES.txt`, and the source of those versions can also be requested from Pinol's maintainers for three years.

**Linux package.** Qt, FFmpeg and their dependencies are bundled as shared libraries in `usr/lib` of the portable
directory (extract an AppImage with `--appimage-extract` to obtain the same directory). Compatible modified LGPL
libraries can be substituted there, then the application launched with `AppRun`. `usr/bin/THIRD-PARTY-LINUX.md`
records the bundled binaries and their source package versions; copyright and licence texts are in
`usr/bin/licenses/linux`. FFmpeg is built from checksummed source without GPL components; its exact source URL,
version and configuration are recorded in `usr/bin/DEPENDENCIES.txt`.

**macOS package.** It also contains the libraries Qt and FFmpeg depend on (images, fonts, compression, encryption,
AV1 decoding…), copied from Homebrew when the package is built. Their list, versions, licences and licence texts are
generated with the package: `THIRD-PARTY-MACOS.md` and `licenses/macos/` in `Pinol.app/Contents/Resources`. The
package's FFmpeg is built from source without GPL components (Homebrew's own includes x264 and x265); the build checks
that every FFmpeg library in it is LGPL and that no GPL-only library is in it.
