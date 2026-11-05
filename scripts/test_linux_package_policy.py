import tempfile
import unittest
from pathlib import Path
from linux_package_policy import tiff_runtime_files


class LinuxPackagePolicyTest(unittest.TestCase):
    def test_only_versioned_tiff_runtime_binaries_are_shipped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'lib').mkdir()
            names = ['libtiff.so', 'libtiffxx.so', 'libtiff.so.6', 'libtiff.so.6.2.0',
                     'libtiffxx.so.6', 'libtiff.a', 'libjbig.so.0']
            for name in names:
                (root / 'lib' / name).touch()
            self.assertEqual([path.name for path in tiff_runtime_files(root)],
                             ['libtiff.so.6', 'libtiff.so.6.2.0', 'libtiffxx.so.6'])

    def test_missing_soname_target_is_not_a_runtime_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'lib').mkdir()
            (root / 'lib/libtiff.so.6').symlink_to('missing')
            self.assertEqual(tiff_runtime_files(root), [])


if __name__ == '__main__':
    unittest.main()
