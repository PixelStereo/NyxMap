"""Reviewed Linux runtime origins and selection of versioned libtiff binaries."""
import re

# Ubuntu's package copyright short names do not always identify the library
# licence: ALSA uses a typo, and libXdamage calls its X11-style licence Xorg.
LICENCE_OVERRIDE = {
    'libasound2t64': 'LGPL-2.1+ (copyright file spells it LPGL-2.1+)',
    # Reviewed in upstream libXdamage 1.1.6 COPYING, shipped in the package's
    # Ubuntu copyright notice: https://www.x.org/releases/individual/lib/libXdamage-1.1.6.tar.xz
    'libxdamage1': 'X11-style (Xorg)',
}


def tiff_runtime_files(prefix):
    # Development linker names (*.so) are not runtime dependencies and must not
    # be attributed to libtiff-dev's unrelated utility licence stanzas.
    return sorted(path for path in (prefix / 'lib').glob('libtiff*.so*')
                  if path.is_file() and re.fullmatch(r'libtiff(?:xx)?\.so\.\d+(?:\.\d+)*', path.name))
