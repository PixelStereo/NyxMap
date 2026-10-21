#!/usr/bin/env python3
"""Create the Linux x86_64 AppImage and a FUSE-free portable archive from the same AppDir.

Usage: package_linux.py BUILD_DIR FFMPEG_PREFIX OMT_DIR NDI_DIR OUTPUT_DIR
Run on Ubuntu 24.04 after building Pinol with build_ffmpeg_linux.py's FFmpeg.
Only glibc and the host's graphics driver stack are intentionally external.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

from build_ffmpeg_lgpl import GPL_ONLY, licence_problem

ROOT = Path(__file__).resolve().parents[1]
HOST_LIB = re.compile(r"^(ld-linux[^/]*|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so.*|"
                      r"lib(GL|EGL|GLX|GLdispatch|OpenGL|GLESv[12]|glapi|gbm|drm[^.]*|"
                      r"vulkan[^.]*|nvidia[^.]*)\.so.*)$")
FFMPEG_LIB = re.compile(r"^lib(avcodec|avdevice|avfilter|avformat|avutil|swresample|swscale)\.so\.")


def run(cmd, **kwargs):
    print("+", " ".join(str(x) for x in cmd), flush=True)
    return subprocess.run([str(x) for x in cmd], check=True, **kwargs)


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def elf(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def tools(folder):
    folder.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "packaging/linux/tools.json").read_text())
    result = {}
    for name, entry in lock.items():
        path = folder / (name + (".AppImage" if name != "runtime-x86_64" else ""))
        urllib.request.urlretrieve(entry["url"], path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            sys.exit(f"{name}: checksum mismatch; review and update packaging/linux/tools.json")
        path.chmod(0o755)
        result[name] = path
    return result


def package(build, ffmpeg, omt, ndi, output):
    output.mkdir(parents=True, exist_ok=True)
    app = output / "Pinol.AppDir"
    if app.exists():
        shutil.rmtree(app)
    binary = app / "usr/bin"
    library = app / "usr/lib"
    library.mkdir(parents=True)
    copy(build / "Pinol", binary / "Pinol")
    shutil.copytree(build / "isf", binary / "isf")
    for name in ("LICENSE", "THIRD-PARTY-NOTICES.md"):
        copy(ROOT / name, binary / name)
    shutil.copytree(ROOT / "licenses", binary / "licenses")
    for folder, names in ((omt, ["libomt.so", "libvmx.so", "OMT-LICENSES.txt"]),
                          (ndi, ["libndi.so.6", "Processing.NDI.Lib.Licenses.txt", "NDI-NOTICE.txt"])):
        for name in names:
            copy(folder / name, binary / name)
    copy(ROOT / "deps-linux.txt", binary / "DEPENDENCIES.txt")
    with (binary / "DEPENDENCIES.txt").open("a") as versions:
        versions.write("\nFFmpeg built from source (LGPL):\n" + (ffmpeg / "VERSION").read_text())
        versions.write("\n" + (ndi / "NDI-SDK.txt").read_text())
        versions.write("\nPackaging tools:\n" + (ROOT / "packaging/linux/tools.json").read_text())

    # Camera capture uses Qt's GStreamer backend. Never ship Ubuntu's GPL FFmpeg via
    # an optional Qt FFmpeg backend. Only the LGPL Qt modules/plugins needed here.
    plugins = Path(subprocess.check_output(["/usr/lib/qt6/bin/qmake", "-query", "QT_INSTALL_PLUGINS"], text=True).strip())
    for category in ("platforms", "imageformats", "multimedia", "xcbglintegrations", "egldeviceintegrations",
                     "wayland-graphics-integration-client", "wayland-decoration-client", "wayland-shell-integration",
                     "platforminputcontexts", "networkinformation", "tls"):
        for source in (plugins / category).glob("*.so"):
            if "ffmpeg" not in source.name and "virtualkeyboard" not in source.name:
                copy(source, app / "usr/plugins" / category / source.name)
    for required in ("platforms/libqxcb.so", "platforms/libqwayland-egl.so", "multimedia/libgstreamermediaplugin.so"):
        if not (app / "usr/plugins" / required).is_file():
            sys.exit(f"Required Qt plugin missing: {required}")
    (binary / "qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\n")
    gst = Path("/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
    for source in gst.glob("*.so"):
        copy(source, library / "gstreamer-1.0" / source.name)
    scanner = Path("/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner")
    copy(scanner, app / "usr/libexec/gst-plugin-scanner")
    shutil.copytree("/usr/share/alsa", app / "usr/share/alsa")
    copy(Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"), app / "usr/share/fonts/DejaVuSans.ttf")

    # Libraries loaded with dlopen() by audio, NDI and Native AOT are invisible to ldd.
    for pattern in ("libasound.so.2", "libpulse.so.0", "libavahi-client.so.3", "libudev.so.1",
                    "libssl.so.3", "libcrypto.so.3", "libicuuc.so.*", "libicui18n.so.*", "libicudata.so.*"):
        sources = sorted(Path("/usr/lib/x86_64-linux-gnu").glob(pattern))
        if not sources:
            sys.exit(f"Required runtime library missing: {pattern}")
        for source in sources:
            copy(source, library / source.name)

    tool = tools(output / "tools")
    env = {**os.environ, "APPIMAGE_EXTRACT_AND_RUN": "1", "NO_STRIP": "1",
           "LD_LIBRARY_PATH": f"{ffmpeg}/lib:{library}:{binary}"}
    # linuxdeploy handles desktop metadata and ELF rpaths; the closure below also
    # includes libraries its portable-baseline exclusion list would leave out.
    run([tool["linuxdeploy"], "--appdir", app, "--executable", binary / "Pinol",
         "--desktop-file", ROOT / "packaging/linux/pinol.desktop", "--icon-file", ROOT / "assets/pinol-app.png",
         "--icon-filename", "pinol"], env=env)

    origins = {}
    checked = set()
    while True:
        pending = [p for p in app.rglob("*") if elf(p) and p not in checked]
        if not pending:
            break
        for path in pending:
            checked.add(path)
            result = subprocess.run(["ldd", str(path)], env=env, text=True, capture_output=True)
            if "not found" in result.stdout or result.returncode:
                sys.exit(f"Unresolved dependencies of {path}:\n{result.stdout}\n{result.stderr}")
            for name, source in re.findall(r"^\s*(\S+) => (/\S+)", result.stdout, re.M):
                source = Path(source)
                if source.is_relative_to(app) or HOST_LIB.match(name):
                    continue
                destination = library / name
                if not destination.exists():
                    copy(source, destination)
                    origins[destination] = source

    # Reinstall our launcher after deployment; it also works directly from the tar.
    launcher = app / "AppRun"
    if launcher.exists() or launcher.is_symlink():
        launcher.unlink()
    copy(ROOT / "packaging/linux/AppRun", launcher)
    launcher.chmod(0o755)
    notices = []
    packages = {}
    for path in sorted(p for p in app.rglob("*") if elf(p)):
        if GPL_ONLY.match(path.name):
            sys.exit(f"GPL-only library in package: {path}")
        if FFMPEG_LIB.match(path.name):
            if problem := licence_problem(path):
                sys.exit(f"{path}: {problem}")
            notices.append(f"| {path.relative_to(app)} | FFmpeg { (ffmpeg / 'VERSION').read_text().splitlines()[0] } (LGPL) |")
            continue
        if path.name in ("Pinol", "libndi.so.6", "libomt.so", "libvmx.so"):
            continue
        source = origins.get(path)
        queries = [str(source), str(source.resolve()), "*/" + path.name] if source else ["*/" + path.name]
        owners = set()
        for query in queries:
            result = subprocess.run(["dpkg-query", "-S", query], text=True, capture_output=True)
            owners.update(line.split(": ", 1)[0] for line in result.stdout.splitlines() if ": " in line)
            if owners:
                break
        if not owners:
            sys.exit(f"Cannot identify the source package of {path}")
        for owner in sorted(owners):
            if owner not in packages:
                record = subprocess.check_output(["dpkg-query", "-W", "-f=${binary:Package} ${Version} (source: ${source:Package} ${source:Version})", owner], text=True)
                packages[owner] = record
                copyright = Path("/usr/share/doc") / owner.split(":")[0] / "copyright"
                copy(copyright, binary / "licenses/linux" / owner / "copyright")
            notices.append(f"| {path.relative_to(app)} | {packages[owner]} |")
    shutil.copytree("/usr/share/common-licenses", binary / "licenses/linux/common-licenses")
    (binary / "THIRD-PARTY-LINUX.md").write_text(
        "# Libraries in the Linux package\n\nQt, FFmpeg and their dependencies are dynamically linked. "
        "Distribution copyright notices and licence texts are in `licenses/linux`. "
        "FFmpeg source, configuration and checksums are in DEPENDENCIES.txt. "
        "Ubuntu source packages: https://launchpad.net/ubuntu/+source/ (names/versions below).\n\n"
        "| File | Component / binary and source package versions |\n|---|---|\n" + "\n".join(notices) + "\n")
    (app / "README-Linux.txt").write_text(
        "Pinol — Ubuntu 24.04 or newer, x86_64\n\n"
        "Portable archive: extract the whole folder, then run ./AppRun. No installation or FUSE needed.\n"
        "AppImage: chmod +x Pinol-linux-x86_64*.AppImage, then run it.\n"
        "If FUSE is unavailable: ./Pinol-linux-x86_64.AppImage --appimage-extract-and-run\n"
        "Qt, FFmpeg, audio/camera plugins, NDI and OMT are included. The host provides glibc, a desktop session "
        "and OpenGL 3.3 graphics drivers. Optional Blackmagic hardware needs Desktop Video.\n"
        "OMT discovery needs a running Avahi service; the application reports its absence.\n"
        "Licences and dependency/source versions: usr/bin/THIRD-PARTY-NOTICES.md, THIRD-PARTY-LINUX.md, "
        "DEPENDENCIES.txt and licenses/.\n")
    suffix = "-" + os.environ["GITHUB_REF_NAME"] if os.environ.get("GITHUB_REF_TYPE") == "tag" else ""
    image = output / f"Pinol-linux-x86_64{suffix}.AppImage"
    run([tool["appimagetool"], "--runtime-file", tool["runtime-x86_64"], app, image], env={**env, "ARCH": "x86_64"})
    with tarfile.open(output / f"Pinol-linux-x86_64{suffix}.tar.gz", "w:gz") as archive:
        archive.add(app, arcname="Pinol")


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    package(*(Path(p).resolve() for p in sys.argv[1:]))
