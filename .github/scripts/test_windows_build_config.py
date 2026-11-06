"""Keep the PowerShell workflow readers compatible with centralized Windows dependencies."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WindowsBuildConfigTest(unittest.TestCase):
    def read(self, variable, workflow):
        source = (ROOT / 'build.ps1').read_text()
        pattern = re.search(r'\$' + variable + r' = FromWorkflow "([^"]+)"', source).group(1)
        match = re.search(pattern, workflow)
        self.assertIsNotNone(match, f'{variable} reader does not recognize workflow dependencies')
        return match.group(1)

    def test_central_values_instead_of_action_expressions(self):
        workflow = """env:
  QT_VERSION: '6.8.3'
  QT_ARCH: 'win64_msvc2022_64'
steps:
  - uses: jurplel/install-qt-action@v4
    with:
      version: ${{ env.QT_VERSION }}
      arch: ${{ env.QT_ARCH }}
"""
        self.assertEqual(self.read('qtVersion', workflow), '6.8.3')
        self.assertEqual(self.read('qtArch', workflow), 'win64_msvc2022_64')
        self.assertEqual(self.read('qtVersion', workflow.replace('6.8.3', '6.10.2')), '6.10.2')

    def test_actual_workflow(self):
        workflow = (ROOT / '.github/workflows/build.yml').read_text()
        self.assertRegex(self.read('qtVersion', workflow), r'^\d+\.\d+\.\d+$')
        self.assertRegex(self.read('qtArch', workflow), r'^win64_msvc\d+_64$')
        self.assertRegex(self.read('ffSha', workflow), r'^[a-f0-9]{64}$')
        self.assertTrue(self.read('ffZip', workflow).startswith('ffmpeg-'))
        self.assertTrue(self.read('ffRelease', workflow).startswith('autobuild-'))


if __name__ == '__main__':
    unittest.main()
