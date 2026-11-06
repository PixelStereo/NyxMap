"""Exercise the UI harness with real versioned executables and ImageMagick crops."""
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HELPERS = Path(__file__).resolve().parents[2] / 'test/ui_test_helpers.sh'


def absolute_error(output):
    # ImageMagick 6 prints the count; 7 also includes its normalized value.
    metric = re.fullmatch(r'\s*(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)(?:\s+\([0-9.eE+-]+\))?\s*', output)
    if not metric:
        raise ValueError(f'Invalid ImageMagick AE metric: {output!r}')
    return float(metric.group(1))


class UiScenarioTest(unittest.TestCase):
    def shell(self, command, root):
        return subprocess.run(['bash', '-c', 'source "$1"; OUT="$2"; ' + command,
                               'ui-helper-test', str(HELPERS), str(root)], text=True, capture_output=True)

    def test_imagemagick_metric_formats(self):
        for text, expected in [('0', 0), ('0 (0)', 0), ('50', 50), ('50 (1)', 50), ('1e+03 (0.5)\n', 1000)]:
            self.assertEqual(absolute_error(text), expected)
        for text in ['', 'compare: unable to open image', '0 trailing error']:
            with self.assertRaises(ValueError):
                absolute_error(text)

    def test_versioned_program_and_arguments_with_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            name = 'NyxMap 0.5.2_a1b2c3d4'
            (root / 'application.json').write_text(json.dumps({'executable': name}))
            exe = root / name
            exe.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            exe.chmod(0o755)
            result = self.shell('APP_BIN=$(ui_executable "$OUT") && "$APP_BIN" --endurance-csv "path with spaces.csv"', root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, '--endurance-csv\npath with spaces.csv\n')

    def window_tools(self, root, delayed=False):
        tools = root / 'tools'
        tools.mkdir()
        search = tools / 'xdotool'
        search.write_text('#!/bin/bash\n'
                          '[[ "$*" != *--pid* ]] || exit 2\n'
                          'count=$(cat "$OUT/search-count" 2>/dev/null || echo 0)\n'
                          'echo $((count+1)) > "$OUT/search-count"\n'
                          + ('[ "$count" -ge 2 ] || exit 1\n' if delayed else 'exit 1\n')
                          + 'printf "11\\n22\\n"\n')
        info = tools / 'xwininfo'
        info.write_text('#!/bin/bash\n'
                        'if [ "$2" = 11 ]; then x=1; y=2; w=200; h=100; else x=30; y=40; w=1200; h=800; fi\n'
                        'printf "Absolute upper-left X: %s\\nAbsolute upper-left Y: %s\\nWidth: %s\\nHeight: %s\\n" "$x" "$y" "$w" "$h"\n')
        search.chmod(0o755)
        info.chmod(0o755)
        return 'export OUT; PATH="$OUT/tools:$PATH"; '

    def test_waits_for_late_window_without_pid_property(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            setup = self.window_tools(root, delayed=True)
            result = self.shell(setup + 'ui_wait_for_window "$$" 3 NyxMap && printf "%s,%s" "$X0" "$Y0"', root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, '30,40')
            self.assertGreaterEqual(int((root / 'search-count').read_text()), 3)

    def test_startup_wait_is_bounded_and_detects_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            setup = self.window_tools(root)
            timed = self.shell(setup + 'ui_wait_for_window "$$" 0 NyxMap', root)
            self.assertNotEqual(timed.returncode, 0)
            self.assertIn('Timed out', timed.stderr)
            dead = self.shell(setup + 'sleep 0.01 & dead=$!; wait "$dead"; ui_wait_for_window "$dead" 3 NyxMap', root)
            self.assertNotEqual(dead.returncode, 0)
            self.assertIn('Application exited', dead.stderr)

    @unittest.skipUnless(shutil.which('convert') and shutil.which('compare'), 'ImageMagick required')
    def test_crop_metrics_and_missing_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, color in [('white', 'white'), ('black', 'black')]:
                subprocess.run(['convert', '-size', '20x10', 'xc:' + color, str(root / (name + '.png'))], check=True)
            bright = self.shell('lum white 10x5+0+0', root)
            self.assertEqual(bright.returncode, 0, bright.stderr)
            self.assertAlmostEqual(float(bright.stdout), 255)
            unchanged = self.shell('same white white 10x5+0+0', root)
            changed = self.shell('same white black 10x5+0+0', root)
            self.assertEqual(unchanged.returncode, 0, unchanged.stdout + unchanged.stderr)
            self.assertEqual(absolute_error(unchanged.stdout), 0)
            self.assertEqual(changed.returncode, 1, changed.stdout + changed.stderr)
            self.assertEqual(absolute_error(changed.stdout), 50)
            missing = self.shell('same missing white 10x5+0+0', root)
            self.assertNotEqual(missing.returncode, 0)


if __name__ == '__main__':
    unittest.main()
