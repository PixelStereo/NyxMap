"""Reuse successful checks only for an incremental, Linux-packaging-only change."""
import json
import os
import re
import subprocess
import urllib.request

LINUX_FILES = {
    'scripts/package_linux.py', 'scripts/smoke_linux_package.py', 'scripts/linux_qt_plugins.py',
    'scripts/test_linux_qt_plugins.py', 'scripts/build_ffmpeg_linux.py', 'scripts/build_tiff_linux.py',
    '.github/scripts/ci_scope.py', '.github/scripts/ci_platform_reuse.py',
    '.github/scripts/test_ci_platform_reuse.py', '.github/workflows/build.yml',
}
JOB_NAMES = {
    'macos': 'macOS (Apple Silicon) + tests', 'windows': 'Windows (latest, Qt MSVC 2022) + tests',
    'memory': 'Workspace memory safety (Linux)', 'valgrind': 'Workspace allocation lifetime (Linux)',
    'tsan': 'ThreadSanitizer (video, sound, OSC)', 'ui': 'Interface scenario (Linux)',
    'fuzz': 'Fuzzing (HAP, Snappy, OSC)',
}


def linux_packaging_only(paths):
    return bool(paths) and all(path in LINUX_FILES or path.startswith('packaging/linux/') for path in paths)


def other_workflow_jobs_unchanged(before, after):
    def normalize(text):
        # Changes to Linux packaging or selection/provenance do not change the
        # other jobs. Global workflow settings and all their steps must match.
        text = re.sub(r'^  (scope|linux):\n.*?(?=^  [a-zA-Z0-9_-]+:\n|\Z)', '', text, flags=re.M | re.S)
        return text.replace('needs.scope.outputs.macos', 'needs.scope.outputs.build').replace(
            'needs.scope.outputs.windows', 'needs.scope.outputs.build').strip()
    return normalize(before) == normalize(after)


def successful_jobs(jobs):
    return {key for key, name in JOB_NAMES.items()
            if any(job['name'] == name and job.get('conclusion') == 'success' for job in jobs)}


def previous_successes(payload):
    before = payload.get('before', '')
    head = payload.get('pull_request', {}).get('head', {}).get('sha', '')
    if not all(re.fullmatch(r'[0-9a-f]{40}', sha) for sha in (before, head)):
        return set(), None
    paths = subprocess.check_output(['git', 'diff', '--name-only', before, head], text=True).splitlines()
    if not linux_packaging_only(paths):
        return set(), None
    workflow = '.github/workflows/build.yml'
    old = subprocess.check_output(['git', 'show', f'{before}:{workflow}'], text=True)
    new = subprocess.check_output(['git', 'show', f'{head}:{workflow}'], text=True)
    if not other_workflow_jobs_unchanged(old, new):
        return set(), None
    api = os.environ.get('GITHUB_API_URL', 'https://api.github.com')
    repo = os.environ['GITHUB_REPOSITORY']
    headers = {'Accept': 'application/vnd.github+json', 'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN']}

    def read(path):
        with urllib.request.urlopen(urllib.request.Request(f'{api}/repos/{repo}/{path}', headers=headers), timeout=20) as response:
            return json.load(response)

    runs = read(f'actions/runs?head_sha={before}&event=pull_request&per_page=20')['workflow_runs']
    for run in runs:
        if run['name'] != os.environ.get('GITHUB_WORKFLOW', 'Build') or run['head_sha'] != before:
            continue
        base = payload.get('pull_request', {}).get('base', {}).get('sha')
        prior_pr = next((pr for pr in run.get('pull_requests', [])
                         if pr.get('number') == payload.get('number')), None)
        prior_base = prior_pr.get('base', {}).get('sha') if prior_pr else None
        if not prior_base or not base:
            continue
        if prior_base != base:
            changed_base = subprocess.check_output(['git', 'diff', '--name-only', prior_base, base], text=True).splitlines()
            if not linux_packaging_only(changed_base):
                continue
            prior_workflow = subprocess.check_output(['git', 'show', f'{prior_base}:{workflow}'], text=True)
            current_workflow = subprocess.check_output(['git', 'show', f'{base}:{workflow}'], text=True)
            if not other_workflow_jobs_unchanged(prior_workflow, current_workflow):
                continue
        jobs = []
        for page in range(1, 5):
            batch = read(f'actions/runs/{run["id"]}/jobs?per_page=100&page={page}')['jobs']
            jobs.extend(batch)
            if len(batch) < 100:
                break
        return successful_jobs(jobs), run['html_url']
    return set(), None
