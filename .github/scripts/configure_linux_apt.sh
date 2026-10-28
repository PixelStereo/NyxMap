#!/usr/bin/env bash
# Only for ephemeral GitHub Ubuntu runners. The image's Azure-first mirror
# list can spend the entire job downloading packages at a few kB/s.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true || ${RUNNER_OS:-} != Linux ]]; then
    echo 'This script only configures GitHub Actions Linux runners.' >&2
    exit 1
fi
if [[ ! -f /etc/apt/apt-mirrors.txt ]]; then
    echo 'The runner APT mirror layout changed; inspect its sources before proceeding.' >&2
    exit 1
fi

# Keep Ubuntu's official archives and the runner's signed repository metadata.
# APT separates mirror metadata with a tab, not spaces.
printf '%s\tpriority:1\n%s\tpriority:2\n' \
    'https://archive.ubuntu.com/ubuntu/' \
    'https://security.ubuntu.com/ubuntu/' | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
sudo tee /etc/apt/apt.conf.d/80nyxmap-ci-network >/dev/null <<'APT'
Acquire::Retries "2";
Acquire::http::Timeout "30";
Acquire::https::Timeout "30";
APT
