#!/usr/bin/env python3
"""Fetch the NDI runtime library from the official NDI SDK, for the packages.

Usage: fetch_ndi.py OUT_DIR

Pinol loads the NDI library at run time and looks for it next to the application first (src/engine/Publish.cpp), so
the packages ship it there: Processing.NDI.Lib.x64.dll (Windows), libndi.dylib (macOS), libndi.so.6 (Linux). The NDI SDK
licence lets an application distribute these binaries when its own terms cover the NDI SDK EULA's, with the third
party licences of the library (Processing.NDI.Lib.Licenses.txt) beside it; both are written to OUT_DIR, with a notice
(NDI-NOTICE.txt). The SDK's download is the vendor's and changes with its versions: the SHA-256 of what was used is
printed, for DEPENDENCIES.txt.
"""
import ctypes
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

URLS = {
    "Windows": "https://downloads.ndi.tv/SDK/NDI_SDK/NDI%206%20SDK.exe",
    "Darwin": "https://downloads.ndi.tv/SDK/NDI_SDK_Mac/Install_NDI_SDK_v6_Apple.pkg",
    "Linux": "https://downloads.ndi.tv/SDK/NDI_SDK_Linux/Install_NDI_SDK_v6_Linux.tar.gz",
}
LIBRARY = {"Windows": "Processing.NDI.Lib.x64.dll", "Darwin": "libndi.dylib", "Linux": "libndi.so.6"}

NOTICE = """NDI runtime bundled with Pinol

This application includes the NDI(R) runtime library from the NDI SDK, distributed under the NDI SDK License
Agreement (https://ndi.video/sdk/). NDI(R) is a registered trademark of Vizrt NDI AB. https://ndi.video

The licences of the third-party components of the NDI library are in Processing.NDI.Lib.Licenses.txt.
"""


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def download(url: Path, dest: Path) -> str:
    print(f"downloading {url}", flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": "Pinol-CI"})
    with urllib.request.urlopen(req, timeout=300) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)
    return hashlib.sha256(dest.read_bytes()).hexdigest()


def find(root: Path, name: str):
    hits = [p for p in root.rglob(name) if p.is_file() or p.is_symlink()]
    # The 64-bit Intel / universal build: not an ARM-only or 32-bit one
    hits = [p for p in hits if not any(part.lower() in ("x86", "aarch64-rpi4-linux-gnueabi", "arm-rpi4-linux-gnueabihf")
                                       for part in p.parts)]
    return sorted(hits, key=lambda p: len(p.parts))


def unpack(system: str, archive: Path, work: Path) -> Path:
    tree = work / "sdk"
    if system == "Darwin":
        run(["pkgutil", "--expand-full", archive, tree])
        return tree
    if system == "Windows":
        # The SDK's installer (Inno Setup), quietly: its EULA accepted for Pinol
        run([archive, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", f"/DIR={tree}"])
        return tree
    with tarfile.open(archive) as t:
        t.extractall(work)
    script = next(work.glob("Install_NDI_SDK_v6_Linux.sh"))
    # It shows the licence and asks for its acceptance: accepted for Pinol
    run(["bash", script], input=b"y\n", cwd=work, env={**os.environ, "PAGER": "cat"})
    return work


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    work = Path(os.environ.get("RUNNER_TEMP", out.parent)) / "ndi-sdk"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    system = platform.system()
    url = os.environ.get("NDI_SDK_URL", URLS[system])
    archive = work / Path(url.replace("%20", " ")).name
    sha = download(url, archive)
    print(f"NDI SDK: {url}\n  SHA-256 {sha}")
    tree = unpack(system, archive, work)

    libs = find(tree, LIBRARY[system])
    if system == "Linux" and not libs:
        libs = find(tree, "libndi.so.6*")
    if not libs:
        sys.exit(f"{LIBRARY[system]} not found in the NDI SDK ({tree})")
    lib = libs[0].resolve()
    shutil.copy2(lib, out / LIBRARY[system])
    licences = find(tree, "Processing.NDI.Lib.Licenses.txt")
    if not licences:
        sys.exit("Processing.NDI.Lib.Licenses.txt not found in the NDI SDK: the library may not be distributed without it")
    shutil.copy2(licences[0], out / "Processing.NDI.Lib.Licenses.txt")
    # It loads, and is the NDI library
    ndi = ctypes.CDLL(str(out / LIBRARY[system]))
    for symbol in ("NDIlib_initialize", "NDIlib_send_create", "NDIlib_find_create_v2", "NDIlib_recv_create_v3"):
        getattr(ndi, symbol)
    print("the NDI library loads and exports its sender, finder and receiver")
    (out / "NDI-NOTICE.txt").write_text(NOTICE, encoding="utf-8")
    (out / "NDI-SDK.txt").write_text(f"NDI SDK: {url}\n  SHA-256 {sha}\n  library: {lib.name}\n", encoding="utf-8")
    for p in sorted(out.iterdir()):
        print(f"{p.name}: {p.stat().st_size} bytes")


if __name__ == "__main__":
    main()
