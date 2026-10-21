#!/usr/bin/env python3
"""CI only: verify both Linux distributions on an OS without Qt, FFmpeg, .NET or FUSE.

Usage: smoke_linux_package.py PACKAGE_DIR MEDIA_DIR WORK_DIR
The container gets only these files and smoke_package.py, never the build tree.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

HOST = re.compile(r"^(ld-linux[^/]*|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so.*|"
                  r"lib(GL|EGL|GLX|GLdispatch|OpenGL|GLESv[12]|glapi|gbm|drm[^.]*|"
                  r"vulkan[^.]*|nvidia[^.]*)\.so.*)$")


def check_tree(tree):
    print(f"Checking ELF dependencies in {tree}", flush=True)
    env = {**os.environ, "LD_LIBRARY_PATH": f"{tree}/usr/lib:{tree}/usr/bin"}
    for path in tree.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        output = subprocess.check_output(["ldd", str(path)], env=env, text=True)
        if "not found" in output:
            sys.exit(f"Missing library for {path}:\n{output}")
        for name, location in re.findall(r"^\s*(\S+) => (/.+?)\s+\(0x[0-9a-fA-F]+\)", output, re.M):
            if not Path(location).is_relative_to(tree) and not HOST.match(name):
                sys.exit(f"Unbundled dependency: {path}: {name} => {location}")
    for plugin in ("platforms/libqxcb.so", "platforms/libqwayland-egl.so", "multimedia/libgstreamermediaplugin.so"):
        if not (tree / "usr/plugins" / plugin).is_file():
            sys.exit(f"Missing plugin: {plugin}")


def main():
    packages, media, work = (Path(p).resolve() for p in sys.argv[1:])
    work.mkdir(parents=True, exist_ok=True)
    # Catch accidental changes to the test image which would hide packaging failures.
    installed = subprocess.check_output(["dpkg-query", "-W", "-f=${binary:Package} ${db:Status-Status}\n"], text=True)
    if re.search(r"^(?:libqt6|qt6-|libavcodec|libavformat|libavutil|libswscale|libswresample|dotnet|libfuse)\S* installed$", installed, re.M):
        sys.exit("The clean runtime image must not supply Qt, FFmpeg, .NET or FUSE")
    portable = work / "portable with spaces"
    portable.mkdir()
    print("Extracting the portable archive", flush=True)
    with tarfile.open(next(packages.glob("*.tar.gz"))) as archive:
        archive.extractall(portable, filter="data")
    tree = portable / "Pinol"
    check_tree(tree)
    smoke = Path(__file__).with_name("smoke_package.py")
    subprocess.run([sys.executable, smoke, tree / "AppRun", tree / "usr/bin/isf", media, work / "portable-render"], check=True)
    # The desktop window also starts with these libraries, fonts and device plugins.
    log = work / "desktop.log"
    with log.open("w") as output:
        desktop = subprocess.Popen([tree / "AppRun"], stdout=output, stderr=subprocess.STDOUT, cwd=work)
        try:
            desktop.wait(timeout=5)
            sys.exit(f"Desktop exited during startup ({desktop.returncode}):\n{log.read_text()}")
        except subprocess.TimeoutExpired:
            desktop.terminate()
            try:
                desktop.wait(timeout=10)
            except subprocess.TimeoutExpired:
                desktop.kill()
                desktop.wait()
    print(log.read_text())

    # Exercise the actual AppImage runtime, including its FUSE-free launch path.
    image = next(packages.glob("*.AppImage"))
    print("Extracting and checking the AppImage", flush=True)
    extracted = work / "image"
    extracted.mkdir()
    subprocess.run([image, "--appimage-extract"], cwd=extracted, check=True, stdout=subprocess.DEVNULL)
    tree = extracted / "squashfs-root"
    check_tree(tree)
    subprocess.run([sys.executable, smoke, image, tree / "usr/bin/isf", media, work / "appimage-render"],
                   env={**os.environ, "APPIMAGE_EXTRACT_AND_RUN": "1"}, check=True)
    print("Both Linux packages render with their own Qt/FFmpeg and load their own NDI/OMT, without FUSE")


if __name__ == "__main__":
    main()
