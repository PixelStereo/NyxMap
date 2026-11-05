import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from linux_qt_plugins import missing_plugin_groups, multimedia_backend


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

    def test_available_multimedia_backend_is_required(self):
        for backend in ('gstreamer', 'ffmpeg'):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                for name in ('platforms/libqxcb.so', 'platforms/libqwayland.so',
                             f'multimedia/lib{backend}mediaplugin.so'):
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.touch()
                self.assertEqual(missing_plugin_groups(root), [])
                self.assertEqual(multimedia_backend(root), backend)
                (root / f'multimedia/lib{backend}mediaplugin.so').unlink()
                self.assertEqual(len(missing_plugin_groups(root)), 1)
                self.assertIsNone(multimedia_backend(root))

    def test_launcher_selects_packaged_backend(self):
        launcher = Path(__file__).resolve().parents[1] / 'packaging/linux/AppRun'
        for backend in ('gstreamer', 'ffmpeg'):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                plugin = root / f'usr/plugins/multimedia/lib{backend}mediaplugin.so'
                plugin.parent.mkdir(parents=True)
                plugin.touch()
                binary = root / 'usr/bin/NyxMap'
                binary.parent.mkdir(parents=True)
                binary.write_text("#!/bin/sh\nprintf '%s\\n' \"$QT_MEDIA_BACKEND\"\n")
                binary.chmod(0o755)
                shutil.copyfile(launcher, root / 'AppRun')
                (root / 'AppRun').chmod(0o755)
                result = subprocess.check_output([root / 'AppRun'], text=True,
                    env={**os.environ, 'XDG_CACHE_HOME': str(root / 'cache'), 'QT_MEDIA_BACKEND': 'incorrect'})
                self.assertEqual(result.strip(), backend)


if __name__ == '__main__':
    unittest.main()
