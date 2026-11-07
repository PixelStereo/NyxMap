"""Reuse verified successes for incremental platform-specific CI changes."""
import json
import os
import re
import subprocess
import urllib.request

LINUX_FILES = {
    'scripts/package_linux.py', 'scripts/smoke_linux_package.py', 'scripts/linux_qt_plugins.py',
    'scripts/test_linux_qt_plugins.py', 'scripts/build_ffmpeg_linux.py', 'scripts/build_tiff_linux.py',
    'scripts/package_linux_baseline.sh', 'scripts/linux_package_policy.py', 'scripts/test_linux_package_policy.py',
    '.github/scripts/ci_scope.py', '.github/scripts/ci_platform_reuse.py',
    '.github/scripts/test_ci_platform_reuse.py', '.github/workflows/build.yml',
}
UI_SCENARIO_FILES = {'test/ui_test.sh', 'test/ui_test_helpers.sh', '.github/scripts/test_ui_scenario.py'}
SELECTION_FILES = {'.github/scripts/ci_scope.py', '.github/scripts/ci_platform_reuse.py',
                   '.github/scripts/test_ci_platform_reuse.py'}


def ui_scenario_only(paths):
    return any(path in UI_SCENARIO_FILES for path in paths) and all(
        path in UI_SCENARIO_FILES or path in SELECTION_FILES for path in paths)


JOB_NAMES = {
    'macos': 'macOS (Apple Silicon) + tests', 'windows': 'Windows (latest, Qt MSVC 2022) + tests',
    'memory': 'Workspace memory safety (Linux)', 'valgrind': 'Workspace allocation lifetime (Linux)',
    'tsan': 'ThreadSanitizer (video, sound, OSC)', 'ui': 'Interface scenario (Linux)',
    'fuzz': 'Fuzzing (HAP, Snappy, OSC)',
}


MAC_SHARDS = {f"{JOB_NAMES['macos']} / {shard}" for shard in ('scene-ui', 'isf-media', 'core')}


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
    reused = {key for key, name in JOB_NAMES.items()
              if any(job['name'] == name and job.get('conclusion') == 'success' for job in jobs)}
    if MAC_SHARDS <= {job['name'] for job in jobs if job.get('conclusion') == 'success'}:
        reused.add('macos')
    for job in jobs:
        if job['name'] == 'Linux (latest) + tests':
            steps = {step['name']: step.get('conclusion') for step in job.get('steps', [])}
            if steps.get('Build') == 'success' and steps.get('Engine tests') == 'success':
                reused.add('linux_engine')
    return reused


def _revision_successes(payload):
    from_base = not payload.get('before')
    before = payload.get('before') or payload.get('pull_request', {}).get('base', {}).get('sha', '')
    head = payload.get('pull_request', {}).get('head', {}).get('sha', '')
    if not all(re.fullmatch(r'[0-9a-f]{40}', sha) for sha in (before, head)):
        return set(), None, False
    paths = subprocess.check_output(['git', 'diff', '--name-only', before, head], text=True).splitlines()
    ui_only = ui_scenario_only(paths)
    if not ui_only and not linux_packaging_only(paths):
        return set(), None, False
    workflow = '.github/workflows/build.yml'
    old = subprocess.check_output(['git', 'show', f'{before}:{workflow}'], text=True)
    new = subprocess.check_output(['git', 'show', f'{head}:{workflow}'], text=True)
    if not other_workflow_jobs_unchanged(old, new):
        return set(), None, False
    api = os.environ.get('GITHUB_API_URL', 'https://api.github.com')
    repo = os.environ['GITHUB_REPOSITORY']
    headers = {'Accept': 'application/vnd.github+json', 'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN']}

    def read(path):
        with urllib.request.urlopen(urllib.request.Request(f'{api}/repos/{repo}/{path}', headers=headers), timeout=20) as response:
            return json.load(response)

    event = 'push' if from_base else 'pull_request'
    runs = read(f'actions/runs?head_sha={before}&event={event}&per_page=20')['workflow_runs']
    for run in runs:
        if run['name'] != os.environ.get('GITHUB_WORKFLOW', 'Build') or run['head_sha'] != before:
            continue
        if not from_base:
            base = payload.get('pull_request', {}).get('base', {}).get('sha')
            prior_pr = next((pr for pr in run.get('pull_requests', [])
                             if pr.get('number') == payload.get('number')), None)
            prior_base = prior_pr.get('base', {}).get('sha') if prior_pr else None
            if not prior_base or not base:
                continue
            if prior_base != base:
                changed_base = subprocess.check_output(['git', 'diff', '--name-only', prior_base, base], text=True).splitlines()
                if not (ui_scenario_only(changed_base) if ui_only else linux_packaging_only(changed_base)):
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
        relevant = [job for job in jobs if job['name'] in JOB_NAMES.values() or job['name'] in MAC_SHARDS]
        inherited = bool(relevant) and all(job.get('conclusion') == 'skipped' for job in relevant)
        reused = successful_jobs(jobs)
        if ui_only:
            # Always execute the changed scenario, even if its previous revision
            # passed. Reuse Linux's whole job only when packaging also succeeded.
            reused.discard('ui')
            if any(job['name'] == 'Linux (latest) + tests' and job.get('conclusion') == 'success' for job in jobs):
                reused.add('linux')
        return reused, run['html_url'], inherited
    return set(), None, False


def previous_successes(payload):
    reused, url, inherited = _revision_successes(payload)
    if not inherited:
        return reused, url
    # A previous Linux-only run may itself have reused successful checks. Walk
    # its source ancestors, proving the complete diff again at every revision.
    # An actual failure or missing provenance stops inheritance immediately.
    before = payload.get('before') or payload.get('pull_request', {}).get('base', {}).get('sha')
    revisions = subprocess.check_output(['git', 'rev-list', '--first-parent', '--max-count=5', before], text=True).splitlines()
    for revision in revisions[1:]:
        reused, url, inherited = _revision_successes({**payload, 'before': revision})
        if not inherited:
            return reused, url
    return set(), None
