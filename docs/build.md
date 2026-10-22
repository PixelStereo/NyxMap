# Build and deployment

[Project overview and downloads](../README.md) · [Limitations and remaining work](README.md)

## Under the hood

PINOL is written in **C++17 / Qt 6**, with **FFmpeg** for media decoding, **OpenGL 3.3** for rendering and **Metal-based output presentation on macOS**.

- **HAP-aware playback** — HAP textures are decompressed and kept GPU-compressed where supported, avoiding an unnecessary full RGBA conversion. HAP Q is the preferred fast path on macOS.
- **Hardware-assisted decoding** — VideoToolbox on macOS, with D3D11VA / VAAPI paths on supported systems; CPU fallback when needed.
- **Multi-stream playback controls** — bounded frame queues, shared decoded-frame accounting, upload-buffer budgeting and a shared worker pool for HAP.
- **Performance visibility** — decoder timing (including p95/p99), missed and skipped frames, latency metrics and a multi-video benchmark.
- **Automation and diagnostics** — OSC/OSCQuery, a filterable event log, engine tests, fuzz testing and ThreadSanitizer checks in CI.

These are engineering capabilities, **not a guarantee of a particular frame rate**: performance depends on codecs, resolution, layer count, effects, outputs and hardware.

## Get started

Download a published build from the [download table](../README.md#download). Successful [GitHub Actions runs](https://github.com/PixelStereo/Pinol/actions/workflows/build.yml) also provide platform packages in their **Artifacts** section. AppImage and autonomous portable Linux packaging starts with the next release after v0.2.

| macOS | Windows | Linux |
| :--- | :--- | :--- |
| Apple Silicon build; a `.app` in a ZIP | MSVC x64 build; Qt and FFmpeg DLLs included | Ubuntu 24.04+ x86_64; AppImage and autonomous portable archive |
| Ad-hoc signed; **not notarized** | Includes Visual C++ runtime installer | Qt, LGPL FFmpeg, camera/audio plugins, NDI and OMT included |

On macOS, extract the ZIP and open `Pinol.app`. It is not Developer ID signed or notarized: macOS may require approval in **System Settings → Privacy & Security**. On Windows, extract the whole ZIP, run the included `vc_redist.x64.exe` if needed, then open `Pinol.exe`.

For Linux, choose either package:

- **Portable archive:** extract the entire archive and launch `Pinol/AppRun`. No installer, administrator access or FUSE required.
- **AppImage:** make it executable (`chmod +x Pinol-linux-x86_64*.AppImage`) and run it. If mounting is unavailable, use `./Pinol-linux-x86_64-VERSION.AppImage --appimage-extract-and-run`, replacing `VERSION` with its release tag. This needs no FUSE installation.

The Linux baseline is **Ubuntu 24.04, x86_64**; older glibc releases and other architectures are unsupported. The host supplies glibc, a desktop session and OpenGL 3.3 graphics drivers. OMT discovery uses the host's Avahi service and reports when it is unavailable. Optional Blackmagic hardware needs Desktop Video. These system services and hardware drivers are not application libraries.

**Legacy v0.2 Linux archive:** this older package is not autonomous. On Ubuntu 24.04 it needs `sudo apt install libqt6widgets6 libqt6openglwidgets6 libqt6opengl6 libqt6multimedia6 qt6-qpa-plugins libgl1 libasound2t64 libavformat60 libavcodec60 libswscale7 libswresample4 libavutil58`. Extract it and run `./Pinol`. Use a newer autonomous package to avoid installing these libraries.

OMT (Open Media Transport, built from source, MIT) and NDI (from the NDI SDK, under its licence agreement) are included. See [known problems](README.md#known-problems) and [third-party notices](../THIRD-PARTY-NOTICES.md).

### Linux release packaging

The [CI workflow](../.github/workflows/build.yml) builds shared FFmpeg from pinned, checksummed source without GPL/nonfree components (with OpenSSL for TLS and libsrt for SRT streams), then builds and tests Pinol against it. `scripts/package_linux.py` creates one AppDir containing the application, shaders, Qt X11/Wayland and image plugins, an explicit list of GStreamer plugins for camera capture, audio libraries, NDI and Native AOT OMT. It closes the ELF dependencies of everything it copies (`ldd`), plus an explicit list of libraries that are loaded with `dlopen()` (ALSA, PulseAudio, Avahi, udev, OpenSSL, ICU, and
with Qt WebEngine the NSS modules); with Qt WebEngine it also adds `QtWebEngineProcess` (`usr/libexec`), its resources
(`usr/resources`) and locales (`usr/translations/qtwebengine_locales`), which `AppRun` points Qt WebEngine to; glibc and the host graphics stack are left out on purpose. Other dynamically loaded modules are not bundled: GIO modules, VA-API (libva) drivers and GStreamer's network, audio and playback plugins come from the host or are absent. The Qt TIFF image plugin is left out (Ubuntu's libtiff links the GPL-only libjbig).

The packaging fails if a library in the package comes from a Debian package whose copyright file declares only GPL licences (no LGPL or permissive one), or is one of the known GPL-only libraries; packages it cannot read correctly are reviewed by hand in `LICENCE_OVERRIDE`. The launcher (`AppRun`) sets library, plugin and GStreamer/ALSA/fontconfig variables for Pinol only: programs Pinol opens through `xdg-open` (browser, file manager) get the environment it was started with back, and GStreamer uses its own registry in `~/.cache/pinol`.

`packaging/linux/tools.json` pins fixed releases and SHA-256 checksums for the official [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy), [appimagetool](https://github.com/AppImage/appimagetool) and [static AppImage runtime](https://github.com/AppImage/type2-runtime) binaries. A checksum mismatch fails packaging. The static runtime does not require the `libfuse2` package; the portable archive and extraction launch work without FUSE.

Every package contains `DEPENDENCIES.txt` and licence texts. The Linux package also records each bundled library's Ubuntu binary and source package versions (`THIRD-PARTY-LINUX.md`) with their distribution copyright files; the macOS package lists the libraries copied from Homebrew in `THIRD-PARTY-MACOS.md`; the Windows package's versions (Qt, FFmpeg and its licence, NDI SDK, Visual C++ Redistributable) are in its `DEPENDENCIES.txt`. To replace an LGPL library, extract the portable archive (or use `--appimage-extract`), replace compatible shared libraries in `usr/lib`, then launch `AppRun`.

CI checks both final packages in unprivileged **Ubuntu 24.04 and 26.04** containers with **no Qt, FFmpeg, .NET or FUSE installed**, no network and no build tree mounted. It verifies dependency closure, loads the bundled NDI/OMT runtimes and renders a project with an image, a video and a packaged shader. Tag publication waits for platform packaging and the full validation gate.

## Build from source

### In one command

Clone the repository, then run the build script: it installs what is missing, builds Pinol (with OMT, and NDI if you
accept the NDI SDK licence when it asks) and opens it.

```bash
git clone https://github.com/PixelStereo/Pinol.git
cd Pinol
./build.sh          # macOS, Linux (Ubuntu/Debian), and Windows in Git Bash
```

On Windows without Git Bash, double-click `build.cmd` (or run `build.cmd` in a terminal).

| | What it installs |
|---|---|
| macOS | Apple's command line tools, [Homebrew](https://brew.sh) if missing, then `qt ffmpeg cmake pkg-config dotnet git` |
| Ubuntu/Debian | the apt packages listed below (it asks for your password) |
| Windows | with winget: Git, CMake, Ninja, Python, LLVM, the .NET 8 SDK and the Visual Studio 2022 Build Tools (C++); then Qt and an LGPL FFmpeg build in `build-deps\`, at the versions the CI uses |

Options (the same on every system): `--no-install` (use what is installed), `--no-run` (do not open Pinol),
`--accept-ndi-license` or `--no-ndi` (do not ask about NDI), `--clean` (start from an empty build folder), `--debug`.
Run it again after `git pull` to rebuild. CI runs the scripts on fresh macOS, Ubuntu and Windows machines
(`.github/workflows/build-scripts.yml`).

### Step by step

**Requirements:** CMake 3.21+, Qt 6.2+, FFmpeg 6.1+, C++17 and OpenGL 3.3 core. Qt Multimedia (cameras) and Qt SVG (SVG images) are optional: without them, CMake says so and the feature is left out. Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/PixelStereo/Pinol.git
cd Pinol
```

<details>
<summary><strong>macOS — Homebrew</strong></summary>

```bash
brew install qt ffmpeg cmake pkg-config dotnet
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DPINOL_ACCEPT_NDI_SDK_LICENSE=ON  # NDI included: you accept the NDI SDK licence (see below)
cmake --build build -j
```

Run `build/Pinol.app`. The build is not Developer ID signed or notarized.

Without `-DPINOL_ACCEPT_NDI_SDK_LICENSE=ON` the build works the same, without NDI. A build directory configured
before keeps its options: run the `cmake -S …` line again to change them.

</details>

<details>
<summary><strong>Linux — Ubuntu 24.04 or newer</strong></summary>

```bash
sudo apt install build-essential cmake pkg-config qt6-base-dev libqt6opengl6-dev \
  qt6-multimedia-dev qt6-svg-dev qt6-webengine-dev libgl1-mesa-dev libavformat-dev libavcodec-dev \
  libswscale-dev libswresample-dev libavutil-dev libasound2-dev \
  git clang dotnet-sdk-8.0  # OMT (see below)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPINOL_ACCEPT_NDI_SDK_LICENSE=ON  # NDI included: you accept the NDI SDK licence (see below)
cmake --build build -j
./build/Pinol
```

</details>

<details>
<summary><strong>Windows — MSVC 2022</strong></summary>

Install Qt 6 (MSVC 2022, including Qt Multimedia, and Qt WebEngine with Qt WebChannel and Qt Positioning for the Web
Page generator), shared FFmpeg (an LGPL build), CMake, and for OMT git, LLVM's
clang and the .NET SDK. From an MSVC developer shell:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" -DFFMPEG_ROOT="C:/ffmpeg" `
  -DPINOL_ACCEPT_NDI_SDK_LICENSE=ON  # NDI included: you accept the NDI SDK licence (see below)
cmake --build build --config Release
```

See [the CI workflow](../.github/workflows/build.yml) for the tested toolchain and deployment steps.

</details>

**Web pages (optional).** The Web Page generator (Media Bin ▸ Generators, or Layer ▸ Load Web Page…) needs Qt
WebEngine: Homebrew's `qt` includes it (macOS 14 or later), on Ubuntu it is `qt6-webengine-dev`, on Windows the
aqt modules `qtwebengine qtwebchannel qtpositioning`. Without it Pinol builds the same, without the generator
(`-DPINOL_WEB=OFF` leaves it out on purpose). It makes the packages larger: Chromium is about 150 MB uncompressed
(roughly 60–70 MB more in each archive). On Linux, where unprivileged user namespaces are restricted (Ubuntu 24.04's
AppArmor default), Chromium's sandbox may not start: a page then says its renderer stopped. Allowing it
(`QTWEBENGINE_DISABLE_SANDBOX=1` in the environment) is a choice for trusted pages only.

The first build also puts OMT next to the program, as in the packages: it is built from source, which needs git,
clang++ (Xcode's on macOS) and the [.NET SDK](https://dotnet.microsoft.com/download), version 8 or later (without one of
them, CMake warns and OMT is left out). The NDI runtime is only downloaded from the NDI SDK if you accept the
[NDI SDK License Agreement](https://ndi.video/sdk/): the download accepts it on your behalf, so it is off by default;
configure with `-DPINOL_ACCEPT_NDI_SDK_LICENSE=ON` to accept it (or point Pinol at an installed NDI runtime). Both go to
`build/runtimes`; `-DPINOL_BUNDLE_RUNTIMES=OFF` leaves them out. `PINOL_BUNDLE_RUNTIMES` defaults to OFF when the `CI`
environment variable is set (the CI packaging steps fetch and build them themselves), and `PINOL_ACCEPT_NDI_SDK_LICENSE` to
ON.

<details>
<summary><strong>Tests, benchmarking and command-line render</strong></summary>

Generate media fixtures and run the engine tests:

```bash
sh test/make_media.sh
cmake --build build --target pinol_tests
ctest --test-dir build --output-on-failure
```

Render a project from the command line:

```bash
Pinol --render out.png --frames 1 --strict project.pinol
```

Benchmark multiple 4K streams:

```bash
cmake --build build --target pinol_bench
sh test/bench_4k.sh build
```

CI runs platform tests, the HAP/OSC fuzzers and selected ThreadSanitizer suites. See the [limitations and roadmap](README.md).

</details>


