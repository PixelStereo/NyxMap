# TouchDesigner components


Windows and macOS can import `.tox` files through TouchEngine. The SDK is optional: before downloading it,
`build.sh` / `build.ps1` ask whether you have accepted the TouchDesigner licence and accept the SDK's
[Shared Use License](https://github.com/PixelStereo/NyxMap/blob/main/licenses/touchengine.txt). Non-interactive builds leave it disabled unless given
`--accept-touchengine-license` (or `-DNYXMAP_ACCEPT_TOUCHENGINE_LICENSE=ON` with CMake).
Use `--no-touchengine` to decline without a prompt. Changing the flag reconfigures an existing build.
The platform CI builds explicitly enable it and include the SDK and notices in their packages.
On macOS, CMake stages and ad-hoc signs the SDK and its nested components for local builds, including test
executables. This requires Apple's command-line tools; no Developer ID is required. Re-running `build.sh`
repairs existing build directories. These local signatures do not replace Developer ID signing or notarization for distribution.

The SDK is pinned to Windows `af3e4a393086c0377cd3634172a0ce18d1a654c8` and macOS
`c3ceb1ab540db44dbfed4fb1bdb7bbfbb0305962`. Only the SDK shim is bundled; install TouchDesigner or TouchPlayer
separately and activate an Educational, Commercial or Pro licence. A Non-Commercial licence does not enable
TouchEngine in third-party software. See [Derivative's requirements](https://derivative.ca/UserGuide/TouchEngine).
NyxMap does not accept or activate the end user's TouchDesigner licence. `TOUCHENGINE_APP_PATH` can select a
particular installation; otherwise the SDK locates it.

Import the `.tox` into the Media Bin and drop it onto a layer. Source › TouchDesigner shows its output TOP,
frame rate, play/pause, reload and exposed parameters. Each input TOP can select a different NyxMap layer's picture after Color and FX, before Spatial
mapping, including a hidden layer. Self-connections, viewport inputs and indirect feedback loops are rejected.
RGB(A) parameters support the same combined color timeline and context menu as other NyxMap colors.
TOP connections and parameter values survive project saves and snapshots. Component-relative resources
should be packaged with TouchDesigner: Collect Files copies the `.tox`, but cannot discover its external dependencies.

CI compiles the native bridges on both platforms and checks routing, persistence, parameters and deletion using a
fake component, without requiring a paid runtime. Licensed playback still needs verification on a real machine:
open a component with two input TOPs and two output TOPs, route two differently colored layers, change and
animate its color, switch outputs, save/reopen, reload, pause/resume, resize the inputs and delete a connected layer.
Check orientation and channels with an asymmetric RGBA test image. Verify the missing-runtime/licence error on an
unlicensed machine. CHOP, DAT, TouchDesigner audio and feedback between layers are not implemented.
