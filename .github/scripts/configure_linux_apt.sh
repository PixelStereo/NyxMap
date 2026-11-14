#!/usr/bin/env bash
# Only for ephemeral GitHub Ubuntu runners. Prefer the public Ubuntu mirrors
# when reachable, with the runner's own mirrors available as fallbacks.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true || ${RUNNER_OS:-} != Linux ]]; then
    echo 'This script only configures GitHub Actions Linux runners.' >&2
    exit 1
fi

mirror_list=/etc/apt/apt-mirrors.txt
if [[ -f $mirror_list ]]; then
    preferred=()
    for host in archive.ubuntu.com security.ubuntu.com; do
        if timeout 5s getent ahosts "$host" >/dev/null; then
            preferred+=("https://$host/ubuntu/")
        fi
    done

    if ((${#preferred[@]} > 0)); then
        # Read before tee truncates the file. Keep other mirrors and their
        # selection metadata, assigning lower priority than the public mirrors.
        fallbacks=$(awk '
            $1 ~ /^https?:\/\// && $1 !~ /^https?:\/\/(archive|security)\.ubuntu\.com\/ubuntu\/?$/ {
                printf "%s\tpriority:%d", $1, NR + 2
                for (i = 2; i <= NF; ++i)
                    if ($i !~ /^priority:/) printf " %s", $i
                print ""
            }
        ' "$mirror_list")
        {
            for i in "${!preferred[@]}"; do
                printf '%s\tpriority:%d\n' "${preferred[i]}" "$((i + 1))"
            done
            printf '%s\n' "$fallbacks"
        } | sudo tee "$mirror_list" >/dev/null
    else
        echo 'Public Ubuntu mirrors did not resolve; keeping the runner mirror list.'
    fi
fi

# Sort after the runner's zz-retries file. Let APT move to the next mirror
# promptly and stop before installation if the package indexes cannot refresh.
sudo tee /etc/apt/apt.conf.d/zzzz-nyxmap-ci-network >/dev/null <<'APT'
Acquire::Retries "0";
Acquire::http::Timeout "15";
Acquire::https::Timeout "15";
APT::Update::Error-Mode "any";
APT
