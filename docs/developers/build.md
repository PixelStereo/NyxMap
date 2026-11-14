# Build from source

## Architecture

NYXMAP is written in **C++17 / Qt 6**, with **FFmpeg** for media decoding, **OpenGL 3.3** for rendering and **Metal-based output presentation on macOS**.

- **HAP-aware playback** — HAP textures are decompressed and kept GPU-compressed where supported, avoiding an unnecessary full RGBA conversion. HAP Q is the preferred fast path on macOS.
- **Hardware-assisted decoding** — VideoToolbox on macOS, with D3D11VA / VAAPI paths on supported systems; CPU fallback when needed.
- **Multi-stream playback controls** — bounded frame queues, shared decoded-frame accounting, upload-buffer budgeting and a shared worker pool for HAP.
- **Performance visibility** — decoder timing (including p95/p99), missed and skipped frames, latency metrics and a multi-video benchmark.
- **Automation and diagnostics** — OSC/OSCQuery, a filterable event log, engine tests, fuzz testing and ThreadSanitizer checks in CI.

These are engineering capabilities, **not a guarantee of a particular frame rate**: performance depends on codecs, resolution, layer count, effects, outputs and hardware.

## Build

### In one command

Clone the repository, then run the build script: it installs what is missing, builds NyxMap (with OMT, and NDI if you
accept the NDI SDK licence when it asks) and opens it.

```bash
git clone https://github.com/PixelStereo/NyxMap.git
cd NyxMap
./build.sh          # macOS, Linux (Ubuntu/Debian), and Windows in Git Bash
```

On Windows without Git Bash, double-click `build.cmd` (or run `build.cmd` in a terminal).

| | What it installs |
|---|---|
| macOS | Apple's command line tools, [Homebrew](https://brew.sh) if missing, then `qt ffmpeg cmake pkg-config dotnet git` |
| Ubuntu/Debian | the apt packages listed below (it asks for your password) |
| Windows | with winget: Git, CMake, Ninja, Python, LLVM, the .NET 8 SDK and the Visual Studio 2022 Build Tools (C++); then Qt and an LGPL FFmpeg build in `build-deps\`, at the versions the CI uses |

Options (the same on every system): `--no-install` (use what is installed), `--no-run` (do not open NyxMap),
`--accept-ndi-license` or `--no-ndi` (do not ask about NDI), `--clean` (start from an empty build folder), `--debug`.
Run it again after `git pull` to rebuild. CI runs the scripts on fresh macOS, Ubuntu and Windows machines
(`.github/workflows/build-scripts.yml`).

### Step by step

**Requirements:** CMake 3.21+, Qt 6.2+, FFmpeg 6.1+, C++17 and OpenGL 3.3 core. Qt Multimedia (cameras) and Qt SVG (SVG images) are optional: without them, CMake says so and the feature is left out. Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/PixelStereo/NyxMap.git
cd NyxMap
```

<details>
<summary><strong>macOS — Homebrew</strong></summary>

```bash
brew install qt ffmpeg cmake pkg-config dotnet
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON  # NDI included: you accept the NDI SDK licence (see below)
cmake --build build -j
```

Run `build/NyxMap.app`. The build is not Developer ID signed or notarized.

Without `-DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON` the build works the same, without NDI. A build directory configured
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
  -DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON  # NDI included: you accept the NDI SDK licence (see below)
cmake --build build -j
./build/NyxMap
```

</details>

<details>
<summary><strong>Windows — MSVC 2022</strong></summary>

Install Qt 6 (MSVC 2022, including Qt Multimedia, and Qt WebEngine with Qt WebChannel and Qt Positioning for the Web
Page generator), shared FFmpeg (an LGPL build), CMake, and for OMT git, LLVM's
clang and the .NET SDK. From an MSVC developer shell:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" -DFFMPEG_ROOT="C:/ffmpeg" `
  -DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON  # NDI included: you accept the NDI SDK licence (see below)
cmake --build build --config Release
```

See [the CI workflow](https://github.com/PixelStereo/NyxMap/blob/main/.github/workflows/build.yml) for the tested toolchain and deployment steps.

</details>

**Web pages (optional).** The Web Page generator (Media Bin ▸ Generators, or Layer ▸ Load Web Page…) needs Qt
WebEngine: Homebrew's `qt` includes it (macOS 14 or later), on Ubuntu it is `qt6-webengine-dev`, on Windows the
aqt modules `qtwebengine qtwebchannel qtpositioning`. Without it NyxMap builds the same, without the generator
(`-DNYXMAP_WEB=OFF` leaves it out on purpose). It makes the packages larger: Chromium is about 150 MB uncompressed
(roughly 60–70 MB more in each archive). On Linux, where unprivileged user namespaces are restricted (Ubuntu 24.04's
AppArmor default), Chromium's sandbox may not start: a page then says its renderer stopped. Allowing it
(`QTWEBENGINE_DISABLE_SANDBOX=1` in the environment) is a choice for trusted pages only.

The first build also puts OMT next to the program, as in the packages: it is built from source, which needs git,
clang++ (Xcode's on macOS) and the [.NET SDK](https://dotnet.microsoft.com/download), version 8 or later (without one of
them, CMake warns and OMT is left out). The NDI runtime is only downloaded from the NDI SDK if you accept the
[NDI SDK License Agreement](https://ndi.video/sdk/): the download accepts it on your behalf, so it is off by default;
configure with `-DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON` to accept it (or point NyxMap at an installed NDI runtime). Both go to
`build/runtimes`; `-DNYXMAP_BUNDLE_RUNTIMES=OFF` leaves them out. `NYXMAP_BUNDLE_RUNTIMES` defaults to OFF when the `CI`
environment variable is set (the CI packaging steps fetch and build them themselves), and `NYXMAP_ACCEPT_NDI_SDK_LICENSE` to
ON.

<details>
<summary><strong>Tests, benchmarking and command-line render</strong></summary>

Generate media fixtures and run the engine tests:

```bash
sh test/make_media.sh
cmake --build build --target nyxmap_tests
ctest --test-dir build --output-on-failure
```

Render a project from the command line:

```bash
NyxMap --render out.png --frames 1 --strict project.nyxmap
```

Play a show headless and keep a picture every 1/25 s, numbered in place of `####` (its startup sequence played, its
sound on a silent device, heard by the audio interactions and the ISF audio inputs). Make a video of the pictures with the
soundtrack:

```bash
NyxMap --render frames/f-####.jpg --play --seconds 30 --every 0.04 project.nyxmap
```

Benchmark multiple 4K streams:

```bash
cmake --build build --target nyxmap_bench
sh test/bench_4k.sh build
```

CI runs platform tests, the HAP/OSC fuzzers and selected ThreadSanitizer suites. See the [limitations](../project/limitations.md) and the [roadmap](../project/roadmap.md).

Release versions come from the build's Git tag (`v0.3.0` or `0.3.0`), including preview suffixes in About and `--version`. CMake also uses its numeric part for the macOS bundle. Untagged source builds fall back to `VERSION`; `-DNYXMAP_VERSION=0.3.0` supports source archives and explicit builds.

After the tag workflow has validated and uploaded all four packages, it calls the **Release metadata** workflow. This updates only the marked download block in the root README and `VERSION` on the default branch, using real asset URLs from the newest complete published release. Published, edited or deleted releases also run this synchronization; it can be run manually from Actions. The workflow uses `GITHUB_TOKEN` with `contents: write` and ordinary fast-forward pushes. The default branch must allow those bot commits. No token with additional permissions is needed.

</details>

## Application names and versions

Release tag builds produce names such as `NyxMap 0.5.2` (`NyxMap 0.5.2.app` on macOS, `NyxMap 0.5.2.exe` on Windows). Personal builds use the nearest preceding version tag plus the eight-character Git commit, for example `NyxMap 0.5.2_a1b2c3d4`. The name is also shown in the window and About dialog. Preferences retain the same application identity.

Without Git metadata, the base version comes from `VERSION` and the revision is `unknown`. `-DNYXMAP_VERSION=0.5.2` overrides the base version; `-DNYXMAP_RELEASE_BUILD=ON` explicitly builds an unsuffixed release from a source archive. Normal release CI uses the tag automatically. `build/application.json` records the generated name and executable for build and packaging scripts.
