import io
import json
import unittest
from unittest.mock import patch
from ci_platform_reuse import linux_packaging_only, other_workflow_jobs_unchanged, successful_jobs, previous_successes

WORKFLOW = '''name: Build
jobs:
  scope:
    outputs: {}
  linux:
    steps: old-linux
  macos:
    if: needs.scope.outputs.build == 'true'
    steps: unchanged-mac
  windows:
    if: needs.scope.outputs.build == 'true'
    steps: unchanged-windows
'''


class PlatformReuseTest(unittest.TestCase):
    def test_only_linux_packaging_paths(self):
        self.assertTrue(linux_packaging_only(['scripts/build_tiff_linux.py', 'packaging/linux/AppRun']))
        for path in ('src/engine/Model3D.cpp', 'src/ui/MainWindow.cpp', 'test/scene3d.cpp', 'VERSION', 'licenses/LGPL-3.0.txt'):
            self.assertFalse(linux_packaging_only(['scripts/package_linux.py', path]))
        self.assertFalse(linux_packaging_only([]))

    def test_only_linux_and_selection_workflow_changes(self):
        newer = WORKFLOW.replace('old-linux', 'new-linux').replace('outputs: {}', 'outputs: expanded')
        newer = newer.replace('needs.scope.outputs.build', 'needs.scope.outputs.macos', 1)
        self.assertTrue(other_workflow_jobs_unchanged(WORKFLOW, newer))
        self.assertFalse(other_workflow_jobs_unchanged(WORKFLOW, newer.replace('unchanged-mac', 'changed-mac')))
        self.assertFalse(other_workflow_jobs_unchanged(WORKFLOW, newer.replace('unchanged-windows', 'changed-windows')))
        self.assertFalse(other_workflow_jobs_unchanged(WORKFLOW, newer.replace('name: Build', 'name: Different')))

    def test_failed_skipped_and_missing_jobs_are_not_reused(self):
        jobs = [{'name': 'macOS (Apple Silicon) + tests', 'conclusion': 'failure'},
                {'name': 'Windows (latest, Qt MSVC 2022) + tests', 'conclusion': 'success'},
                {'name': 'ThreadSanitizer (video, sound, OSC)', 'conclusion': 'skipped'}]
        self.assertEqual(successful_jobs(jobs), {'windows'})
        self.assertEqual(successful_jobs([]), set())

    def test_verified_previous_head_and_base(self):
        before, head, base = 'a' * 40, 'b' * 40, 'c' * 40
        payload = {'before': before, 'number': 337, 'pull_request': {'head': {'sha': head}, 'base': {'sha': base}}}
        run = {'id': 123, 'name': 'Build', 'head_sha': before, 'html_url': 'https://github.com/example/run/123',
               'pull_requests': [{'number': 337, 'base': {'sha': base}}]}
        jobs = [{'name': 'Windows (latest, Qt MSVC 2022) + tests', 'conclusion': 'success'},
                {'name': 'macOS (Apple Silicon) + tests', 'conclusion': 'failure'}]
        responses = [io.StringIO(json.dumps({'workflow_runs': [run]})), io.StringIO(json.dumps({'jobs': jobs}))]
        with patch.dict('os.environ', {'GITHUB_REPOSITORY': 'example/repo', 'GITHUB_TOKEN': 'test-placeholder', 'GITHUB_WORKFLOW': 'Build'}), \
             patch('subprocess.check_output', side_effect=['scripts/package_linux.py\n', WORKFLOW, WORKFLOW]), \
             patch('urllib.request.urlopen', side_effect=responses):
            reused, url = previous_successes(payload)
        self.assertEqual(reused, {'windows'})
        self.assertEqual(url, run['html_url'])

    def test_run_without_base_provenance_is_not_reused(self):
        before, head, base = 'a' * 40, 'b' * 40, 'c' * 40
        payload = {'before': before, 'number': 337, 'pull_request': {'head': {'sha': head}, 'base': {'sha': base}}}
        run = {'id': 123, 'name': 'Build', 'head_sha': before, 'pull_requests': []}
        with patch.dict('os.environ', {'GITHUB_REPOSITORY': 'example/repo', 'GITHUB_TOKEN': 'test-placeholder', 'GITHUB_WORKFLOW': 'Build'}), \
             patch('subprocess.check_output', side_effect=['scripts/package_linux.py\n', WORKFLOW, WORKFLOW]), \
             patch('urllib.request.urlopen', return_value=io.StringIO(json.dumps({'workflow_runs': [run]}))):
            self.assertEqual(previous_successes(payload), (set(), None))


if __name__ == '__main__':
    unittest.main()
