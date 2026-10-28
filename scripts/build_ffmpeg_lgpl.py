#!/usr/bin/env python3
"""FFmpeg for the macOS package: the version Homebrew installed, rebuilt from source without its GPL components.

Usage:
  build_ffmpeg_lgpl.py build PREFIX    build into PREFIX (skipped when PREFIX already holds that version)
  build_ffmpeg_lgpl.py replace PREFIX  put PREFIX's libraries in place of Homebrew's, for the packaging that follows
  build_ffmpeg_lgpl.py check APP       every FFmpeg library in the app is LGPL, and none of the GPL-only
                                       libraries (x264, x265, postproc…) is in it
  build_ffmpeg_lgpl.py check-dlls DIR  Windows: every FFmpeg DLL in DIR is LGPL v2.1 or later, or LGPL v3 or later
                                       (BtbN's lgpl builds may use --enable-version3); prints the licence last

Homebrew builds FFmpeg with --enable-gpl (libx264, libx265…): an application that ships it is a GPL work, which the
NDI runtime and NyxMap's own terms do not allow. This build has no --enable-gpl and no --enable-nonfree, so it is
LGPL v2.1 or later. NyxMap and Qt's FFmpeg plugin are built against Homebrew's FFmpeg of the same version, so its
libraries keep their names and interface; the tests run against Homebrew's, the package ships these.

Needs Homebrew's ffmpeg and dav1d (AV1 decoding, BSD 2-clause), Xcode's compiler, curl.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# The libraries the package may not contain: GPL-only codecs and filters Homebrew links into its FFmpeg
GPL_ONLY = re.compile(r"^lib(x264|x265|postproc|xvidcore|vidstab|rubberband|frei0r|cdio|davs2|xavs2?)\b")
FFMPEG_LIB = re.compile(r"^lib(avcodec|avdevice|avfilter|avformat|avutil|postproc|swresample|swscale)\.\d+\.dylib$")


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def brew_prefix():
    return Path(subprocess.check_output(["brew", "--prefix", "ffmpeg"], text=True).strip())


def homebrew_version():
    # "ffmpeg 8.0_1": the upstream version, without Homebrew's revision
    line = subprocess.check_output(["brew", "list", "--versions", "ffmpeg"], text=True).split()
    return re.sub(r"_\d+$", "", line[1])


def build(prefix: Path):
    version = homebrew_version()
    stamp = prefix / "VERSION"
    if stamp.is_file() and stamp.read_text().split()[0] == version:
        print(f"FFmpeg {version} (LGPL) already built in {prefix}")
        return
    if prefix.exists():
        shutil.rmtree(prefix)
    work = Path(os.environ.get("RUNNER_TEMP", prefix.parent)) / "ffmpeg-src"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    url = f"https://ffmpeg.org/releases/ffmpeg-{version}.tar.xz"
    archive = work / f"ffmpeg-{version}.tar.xz"
    run(["curl", "-fsSL", "--retry", "3", "-o", archive, url])
    sha = hashlib.sha256(archive.read_bytes()).hexdigest()
    run(["tar", "-xf", archive, "-C", work])
    src = work / f"ffmpeg-{version}"
    # The libraries name each other as Homebrew's do (opt/ffmpeg/lib), so they can stand in for them as they are
    configure = ["--prefix=" + str(prefix), f"--install-name-dir={brew_prefix()}/lib",
                 "--enable-shared", "--disable-static", "--disable-programs", "--disable-doc",
                 "--enable-pthreads", "--enable-videotoolbox", "--enable-audiotoolbox", "--enable-libdav1d",
                 # Nothing from Homebrew found on its own: only what is listed here
                 "--disable-sdl2", "--disable-xlib", "--disable-libxcb", "--disable-vulkan", "--disable-lzma"]
    run(["./configure", *configure], cwd=src)
    run(["make", f"-j{os.cpu_count() or 4}"], cwd=src)
    run(["make", "install"], cwd=src)
    stamp.write_text(f"{version}\n{url}\nSHA-256 {sha}\nconfigure {' '.join(configure[1:])}\n")
    check_licence(prefix / "lib")


def licences_of(lib: Path):
    """The licences an FFmpeg library declares: each one carries its licence ("libavcodec license: LGPL version 2.1 or
    later", "LGPL version 3 or later" with --enable-version3, "GPL version 2 or later" in a GPL build)"""
    return {x.decode() for x in re.findall(rb"lib\w+ license: ([^\0]+)", lib.read_bytes())}


def licence_problem(lib: Path, allow_version3=False):
    """Why this FFmpeg library is not LGPL v2.1 or later, or None. With allow_version3 (the Windows package, whose
    licence texts include LGPL v3) LGPL v3 or later is accepted too. GPL and non-free parts never are: the configure
    line each library carries must not have them. macOS and Linux stay strict."""
    data = lib.read_bytes()
    accepted = [{"LGPL version 2.1 or later"}] + ([{"LGPL version 3 or later"}] if allow_version3 else [])
    if (licences := licences_of(lib)) not in accepted:
        return f"licence {sorted(licences)}"
    if re.search(rb"--enable-(gpl|nonfree)\b", data):
        return "configured with GPL or non-free parts"
    if not allow_version3 and re.search(rb"--enable-version3\b", data):
        return "configured with LGPL v3 parts"
    return None


def check_licence(lib: Path):
    files = [p for p in lib.glob("*.dylib") if FFMPEG_LIB.match(p.name)]
    errors = [f"{p.name}: {why}" for p in files if (why := licence_problem(p.resolve()))]
    if errors or not files:
        sys.exit("\n".join(errors) or f"no FFmpeg library in {lib}")
    print(f"{len(files)} FFmpeg libraries in {lib}: LGPL version 2.1 or later")


def ours(prefix: Path):
    """name (libavcodec.62.dylib) -> the built file"""
    return {p.name: p.resolve() for p in (prefix / "lib").glob("*.dylib") if FFMPEG_LIB.match(p.name)}


def replace(prefix: Path):
    built = ours(prefix)
    lib = brew_prefix() / "lib"
    for link in sorted(lib.glob("*.dylib")):
        if not FFMPEG_LIB.match(link.name):
            continue
        if link.name not in built:
            print(f"warning: {link.name} is not in the LGPL build; the package check fails if anything needs it")
            continue
        target = link.resolve()
        target.chmod(target.stat().st_mode | 0o200)
        shutil.copy2(built[link.name], target)
        print(f"{target} <- {built[link.name]}")
    check_licence(lib)


def check(app: Path):
    found, errors = 0, []
    for p in (app / "Contents").rglob("*.dylib"):
        if not p.is_file() or p.is_symlink():
            continue
        if GPL_ONLY.match(p.name):
            errors.append(f"GPL-only library in the package: {p}")
        elif FFMPEG_LIB.match(p.name):
            found += 1
            if why := licence_problem(p):
                errors.append(f"{p}: {why}")
    if errors or not found:
        sys.exit("\n".join(errors) or f"no FFmpeg library found in {app}")
    print(f"{found} FFmpeg libraries in the package, all LGPL; no GPL-only library")


def check_dlls(folder: Path):
    """Windows: the FFmpeg DLLs next to NyxMap.exe (avcodec-62.dll…), and no GPL-only postproc"""
    files = sorted(p for p in folder.glob("*.dll") if re.match(r"^(av\w+|sw\w+|postproc)-\d+\.dll$", p.name, re.I))
    errors = [f"GPL-only library in the package: {p.name}" for p in files if p.name.lower().startswith("postproc")]
    errors += [f"{p.name}: {why}" for p in files if (why := licence_problem(p, allow_version3=True))]
    if errors or not files:
        sys.exit("\n".join(errors) or f"no FFmpeg DLL in {folder}")
    licences = set()
    for p in files:
        licences |= licences_of(p)
        print(f"{p.name}: {', '.join(sorted(licences_of(p)))}")
    print(f"{len(files)} FFmpeg DLLs, without GPL or non-free parts; licence:")
    # Together they are under the stricter one
    print("LGPL version 3 or later" if "LGPL version 3 or later" in licences else "LGPL version 2.1 or later")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "build":
        build(Path(sys.argv[2]).resolve())
    elif len(sys.argv) == 3 and sys.argv[1] == "replace":
        replace(Path(sys.argv[2]).resolve())
    elif len(sys.argv) == 3 and sys.argv[1] == "check":
        check(Path(sys.argv[2]).resolve())
    elif len(sys.argv) == 3 and sys.argv[1] == "check-dlls":
        check_dlls(Path(sys.argv[2]).resolve())
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
