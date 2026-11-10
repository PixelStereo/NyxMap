# Install and launch

Download the latest build from the [releases page](https://github.com/PixelStereo/NyxMap/releases/latest), or from the [download table](https://github.com/PixelStereo/NyxMap#download) of the project page. Successful [GitHub Actions runs](https://github.com/PixelStereo/NyxMap/actions/workflows/build.yml) also provide platform packages in their **Artifacts** section.

| macOS | Windows | Linux |
| :--- | :--- | :--- |
| Apple Silicon build; a `.app` in a ZIP | MSVC x64 build; Qt and FFmpeg DLLs included | Ubuntu 24.04+ x86_64; AppImage and autonomous portable archive |
| Ad-hoc signed; **not notarized** | Includes Visual C++ runtime installer | Qt, LGPL FFmpeg, camera/audio plugins, NDI and OMT included |

On macOS, extract the ZIP and open `NyxMap.app`. It is not Developer ID signed or notarized: macOS may require approval in **System Settings → Privacy & Security**. On Windows, extract the whole ZIP, run the included `vc_redist.x64.exe` if needed, then open `NyxMap.exe`.

For Linux, choose either package:

- **Portable archive:** extract the entire archive and launch `NyxMap/AppRun`. No installer, administrator access or FUSE required.
- **AppImage:** make it executable (`chmod +x NyxMap-linux-x86_64*.AppImage`) and run it. If mounting is unavailable, use `./NyxMap-linux-x86_64-VERSION.AppImage --appimage-extract-and-run`, replacing `VERSION` with its release tag. This needs no FUSE installation.

The Linux baseline is **Ubuntu 24.04, x86_64**; older glibc releases and other architectures are unsupported. The host supplies glibc, a desktop session and OpenGL 3.3 graphics drivers. OMT discovery uses the host's Avahi service and reports when it is unavailable. Optional Blackmagic hardware needs Desktop Video. These system services and hardware drivers are not application libraries.

OMT (Open Media Transport, built from source, MIT) and NDI (from the NDI SDK, under its licence agreement) are included. See [known problems](../project/limitations.md) and [third-party notices](https://github.com/PixelStereo/NyxMap/blob/main/THIRD-PARTY-NOTICES.md).
