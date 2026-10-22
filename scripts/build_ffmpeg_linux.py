#!/usr/bin/env python3
"""Build the Linux release's shared LGPL FFmpeg, before configuring Pinol.

Usage: build_ffmpeg_linux.py PREFIX
Needs build-essential, nasm, pkg-config, libdav1d-dev, libva-dev, libdrm-dev, zlib1g-dev, libssl-dev and
libsrt-openssl-dev.
--disable-autodetect: only what is listed is linked. FFmpeg's own demuxers, decoders and network protocols are all
built (image2 for image sequences, TIFF/DPX/EXR/TGA/PNG/JPEG, RTSP/RTP/UDP/TCP/HTTP/HLS/RTMP); OpenSSL (Apache-2.0)
adds TLS (https, rtmps, HLS over https) and libsrt (MPL-2.0) the srt:// protocol, both from Ubuntu, bundled by
package_linux.py with their licences. Neither needs --enable-gpl, --enable-nonfree or --enable-version3.
The system ffmpeg program is only used to generate test fixtures; it is never packaged.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

VERSION = "8.1.3"
SHA256 = "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"


def main():
    prefix = Path(sys.argv[1]).resolve()
    url = f"https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz"
    configure = [f"--prefix={prefix}", "--enable-shared", "--disable-static", "--disable-programs",
                 "--disable-doc", "--disable-autodetect", "--enable-pthreads", "--enable-zlib",
                 "--enable-libdav1d", "--enable-vaapi", "--enable-libdrm",
                 "--enable-openssl", "--enable-libsrt"]
    stamp = f"{VERSION}\n{url}\nSHA-256 {SHA256}\nconfigure {' '.join(configure)}\n"
    if (prefix / "VERSION").is_file() and (prefix / "VERSION").read_text() == stamp:
        print(f"Using cached LGPL FFmpeg {VERSION}")
        return
    work = Path(os.environ.get("RUNNER_TEMP", prefix.parent)) / "ffmpeg-linux-src"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    archive = work / "ffmpeg.tar.xz"
    urllib.request.urlretrieve(url, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        sys.exit("FFmpeg source checksum mismatch")
    with tarfile.open(archive) as src:
        src.extractall(work, filter="data")
    source = work / f"ffmpeg-{VERSION}"
    for cmd in (["./configure", *configure], ["make", "-j4"], ["make", "install"]):
        subprocess.run(cmd, cwd=source, check=True)
    (prefix / "VERSION").write_text(stamp)


if __name__ == "__main__":
    main()
