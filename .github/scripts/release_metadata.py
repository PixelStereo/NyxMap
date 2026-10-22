"""Refresh only the generated README downloads and VERSION from published assets."""
import json
import os
from pathlib import Path
import re
import urllib.request

START = "<!-- release-downloads:start -->"
END = "<!-- release-downloads:end -->"


def release_assets(release, repository):
    tag = release["tag_name"]
    if not re.fullmatch(r"v?\d+\.\d+(?:\.\d+)?(?:[-+][0-9A-Za-z.-]+)?", tag):
        return None
    names = {
        "mac": f"Pinol-macos-{tag}.zip",
        "win": f"Pinol-windows-{tag}.zip",
        "linux": f"Pinol-linux-x86_64-{tag}.AppImage",
        "portable": f"Pinol-linux-x86_64-{tag}.tar.gz",
    }
    assets = {a["name"]: a for a in release.get("assets", []) if a.get("state") == "uploaded" and a.get("size", 0) > 0}
    urls = {}
    for platform, name in names.items():
        asset = assets.get(name)
        if not asset:
            return None
        url = asset["browser_download_url"]
        if url != f"https://github.com/{repository}/releases/download/{tag}/{name}":
            raise ValueError(f"Unexpected asset URL: {url}")
        urls[platform] = url
    return urls


def select_release(releases, repository):
    # Edits to old releases never replace a newer publication. Wait until all
    # four assets are uploaded; a partially uploaded release has no dead links.
    published = sorted((r for r in releases if not r.get("draft") and r.get("published_at")),
                       key=lambda r: (r["published_at"], r.get("id", 0)), reverse=True)
    for release in published:
        urls = release_assets(release, repository)
        if urls:
            return release, urls
    raise ValueError("No published release with all four platform packages")


def downloads(release, urls, repository):
    tag = release["tag_name"]
    preview = " (preview)" if release.get("prerelease") else ""
    return f"""{START}
Latest published build: **[{tag}](https://github.com/{repository}/releases/tag/{tag})**{preview}.

| macOS — Apple Silicon | Windows — 64-bit | Ubuntu — 64-bit (x86_64) |
| :---: | :---: | :---: |
| **[Download macOS]({urls['mac']})** | **[Download Windows]({urls['win']})** | **[Download Ubuntu AppImage]({urls['linux']})** |

Linux: Ubuntu 24.04 or newer. The **[portable archive]({urls['portable']})** runs without FUSE or installation. Both Linux packages include Qt, FFmpeg, NDI and OMT; see the [launch instructions](docs/build.md#get-started).
{END}"""


def update_files(root, releases, repository):
    release, urls = select_release(releases, repository)
    path = root / "README.md"
    text = path.read_text(encoding="utf-8")
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) > text.index(END):
        raise ValueError("README download markers must occur exactly once, in order")
    start, end = text.index(START), text.index(END) + len(END)
    path.write_text(text[:start] + downloads(release, urls, repository) + text[end:], encoding="utf-8")
    version = release["tag_name"].removeprefix("v")
    (root / "VERSION").write_text(version + "\n", encoding="utf-8")
    return release["tag_name"]


def fetch_releases(repository, token):
    releases = []
    for page in range(1, 101):
        req = urllib.request.Request(f"https://api.github.com/repos/{repository}/releases?per_page=100&page={page}",
                                     headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json",
                                              "X-GitHub-Api-Version": "2022-11-28"})
        with urllib.request.urlopen(req, timeout=30) as response:
            batch = json.load(response)
        releases.extend(batch)
        if len(batch) < 100:
            return releases
    raise ValueError("Release pagination limit exceeded")


if __name__ == "__main__":
    repository = os.environ["GITHUB_REPOSITORY"]
    tag = update_files(Path.cwd(), fetch_releases(repository, os.environ["GH_TOKEN"]), repository)
    print(f"Download links and source version synchronized to {tag}")
