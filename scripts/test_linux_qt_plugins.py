import tempfile
import unittest
from pathlib import Path

from linux_qt_plugins import missing_plugin_groups


class LinuxQtPluginsTest(unittest.TestCase):
    def test_old_and_consolidated_wayland_plugins(self):
        for wayland in ('libqwayland-egl.so', 'libqwayland.so'):
            with self.subTest(wayland=wayland), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                for name in ('platforms/libqxcb.so', f'platforms/{wayland}',
                             'multimedia/libgstreamermediaplugin.so'):
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.touch()
                self.assertEqual(missing_plugin_groups(root), [])
                (root / f'platforms/{wayland}').unlink()
                self.assertEqual(len(missing_plugin_groups(root)), 1)

    def test_ffmpeg_backend_does_not_replace_gstreamer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('platforms/libqxcb.so', 'platforms/libqwayland.so',
                         'multimedia/libffmpegmediaplugin.so'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            self.assertEqual(missing_plugin_groups(root), ['multimedia/libgstreamermediaplugin.so'])


if __name__ == '__main__':
    unittest.main()
