#!/usr/bin/env python3
"""Rebuild Ubuntu's security-patched libtiff without its optional JBIG dependency.

The runner must enable Ubuntu deb-src entries and install libtiff's codec headers.
APT verifies the signed source indexes and source hashes; use the exact installed
source version so Qt WebEngine keeps its distro ABI and security fixes.
"""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    if sys.platform != 'linux':
        sys.exit('This builder is for the Ubuntu Linux package')
    prefix = Path(sys.argv[1]).resolve()
    version = subprocess.check_output(['dpkg-query', '-W', '-f=${source:Version}', 'libtiff6'], text=True).strip()
    work = Path(os.environ.get('RUNNER_TEMP', prefix.parent)) / 'nyxmap-tiff-source'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    run(['apt-get', 'source', f'tiff={version}'], cwd=work)
    sources = [path for path in work.glob('tiff-*') if (path / 'CMakeLists.txt').is_file()]
    if len(sources) != 1:
        sys.exit('Expected exactly one authenticated libtiff source tree')
    source = sources[0]
    build = work / 'build'
    run(['cmake', '-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release',
         f'-DCMAKE_INSTALL_PREFIX={prefix}', '-DCMAKE_INSTALL_LIBDIR=lib',
         '-DBUILD_SHARED_LIBS=ON', '-Dtiff-static=OFF', '-Dtiff-tools=OFF',
         '-Dtiff-tests=ON', '-Dtiff-contrib=OFF', '-Dtiff-docs=OFF', '-Djbig=OFF'])
    run(['cmake', '--build', build, '-j4'])
    run(['ctest', '--test-dir', build, '--output-on-failure'])
    run(['cmake', '--install', build])
    library = prefix / 'lib/libtiff.so.6'
    needed = subprocess.check_output(['readelf', '-d', library], text=True)
    if 'libjbig' in needed:
        sys.exit('Rebuilt libtiff still requires JBIG')
    loaded = ctypes.CDLL(str(library))
    loaded.TIFFIsCODECConfigured.argtypes = [ctypes.c_uint16]
    loaded.TIFFIsCODECConfigured.restype = ctypes.c_int
    if loaded.TIFFIsCODECConfigured(34661) or not all(loaded.TIFFIsCODECConfigured(codec) for codec in (1, 5, 7, 8, 32773)):
        sys.exit('Unexpected TIFF codec configuration (require baseline, LZW, JPEG, Deflate and PackBits; no JBIG)')
    shutil.copyfile(source / 'LICENSE.md', prefix / 'TIFF-LICENSE.md')
    (prefix / 'VERSION').write_text(f'Ubuntu source tiff {version}\nJBIG disabled; upstream tests passed\n')


if __name__ == '__main__':
    main()
