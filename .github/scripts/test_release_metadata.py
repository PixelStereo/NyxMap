import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import release_metadata as metadata

REPO = "PixelStereo/NyxMap"
ROOT = Path(__file__).resolve().parents[2]


def release(tag="v1.2.3", date="2026-10-22T10:00:00Z", **flags):
    names = [f"NyxMap-macos-arm64-{tag}.zip", f"NyxMap-windows-x64-{tag}-Setup.exe",
             f"NyxMap-linux-x64-{tag}.AppImage", f"NyxMap-linux-x64-{tag}.tar.gz"]
    return {"tag_name": tag, "published_at": date, "draft": False, "prerelease": False, **flags,
            "assets": [{"name": name, "state": "uploaded", "size": 123,
                        "browser_download_url": f"https://github.com/{REPO}/releases/download/{tag}/{name}"}
                       for name in names]}


class ReleaseMetadataTests(unittest.TestCase):
    def test_intel_zip_gets_its_own_badge(self):
        current = release()
        name = "NyxMap-macos-x64-v1.2.3.zip"
        current["assets"].append({"name": name, "state": "uploaded", "size": 123,
                                  "browser_download_url": f"https://github.com/{REPO}/releases/download/v1.2.3/{name}"})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text(f"{metadata.START}\nold\n{metadata.END}\n", encoding="utf-8")
            metadata.update_files(root, [current], REPO)
            text = (root / "README.md").read_text(encoding="utf-8")
            self.assertIn("macOS-Intel", text)
            self.assertIn(name, text)

    def test_release_before_the_shared_names_keeps_its_older_files(self):
        old = release()
        for asset in old["assets"]:
            for new_name, old_name in (("-macos-arm64-", "-macos-"), ("-windows-x64-", "-windows-"), ("-linux-x64-", "-linux-x86_64-")):
                asset["name"] = asset["name"].replace(new_name, old_name)
                asset["browser_download_url"] = asset["browser_download_url"].replace(new_name, old_name)
            if asset["name"].endswith("-Setup.exe"):
                asset["name"] = asset["name"].replace("-Setup.exe", ".zip")
                asset["browser_download_url"] = asset["browser_download_url"].replace("-Setup.exe", ".zip")
        chosen, urls = metadata.select_release([old], REPO)
        self.assertTrue(urls["win"].endswith("-windows-v1.2.3.zip"))
        self.assertTrue(urls["linux"].endswith("-linux-x86_64-v1.2.3.AppImage"))

    def test_actual_assets_and_idempotence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = f"before\n{metadata.START}\nstale\n{metadata.END}\nafter\n"
            (root / "README.md").write_text(original, encoding="utf-8")
            current = release()
            metadata.update_files(root, [current], REPO)
            text = (root / "README.md").read_text(encoding="utf-8")
            self.assertTrue(text.startswith("before\n") and text.endswith("\nafter\n"))
            for asset in current["assets"][:3]:
                self.assertIn(asset["browser_download_url"], text)
            self.assertNotIn(current["assets"][3]["browser_download_url"], text)
            self.assertIn("documentation-blue", text)
            self.assertIn("6b7787", text)   # macOS Apple Silicon
            self.assertIn("2b6fb6", text)   # Windows
            self.assertIn("b8821c", text)   # Linux
            self.assertNotIn("macOS-Intel", text)  # this release has no Intel zip: no badge
            self.assertNotIn("Portable archive", text)
            self.assertEqual((root / "VERSION").read_text(), "1.2.3\n")
            metadata.update_files(root, [current], REPO)
            self.assertEqual((root / "README.md").read_text(encoding="utf-8"), text)

    def test_old_edits_drafts_and_incomplete_uploads(self):
        current = release()
        old = release("v1.1.0", "2026-10-21T10:00:00Z", updated_at="2099-01-01T00:00:00Z")
        draft = release("v2.0.0", "2026-10-23T10:00:00Z", draft=True)
        incomplete = release("v3.0.0", "2026-10-24T10:00:00Z")
        for state in ("missing", "new", "empty"):
            partial = copy.deepcopy(incomplete)
            if state == "missing":
                partial["assets"].pop()
            elif state == "new":
                partial["assets"][0]["state"] = "new"
            else:
                partial["assets"][0]["size"] = 0
            chosen, _ = metadata.select_release([old, draft, partial, current], REPO)
            self.assertEqual(chosen["tag_name"], "v1.2.3")

    def test_other_package_names_never_publish(self):
        mixed = release()
        mixed["assets"][0]["name"] = mixed["assets"][0]["name"].replace("NyxMap-", "Other-")
        with self.assertRaises(ValueError):
            metadata.select_release([mixed], REPO)

    def test_preview_and_tag_without_v(self):
        current = release("1.3.0-rc.1", prerelease=True)
        chosen, urls = metadata.select_release([current], REPO)
        self.assertIn("· PREVIEW", metadata.downloads(chosen, urls, REPO))
        self.assertIn("1.3.0-rc.1", metadata.downloads(chosen, urls, REPO))

    def test_bad_urls_and_no_complete_release_fail(self):
        bad = release()
        bad["assets"][0]["browser_download_url"] = "https://example.org/package.zip"
        with self.assertRaises(ValueError):
            metadata.select_release([bad], REPO)
        with self.assertRaises(ValueError):
            metadata.select_release([], REPO)

    def test_companion_link_only_when_asset_is_published(self):
        current = release()
        _, urls = metadata.select_release([current], REPO)
        self.assertNotIn('alt="Module for Companion"', metadata.downloads(current, urls, REPO))
        companion = {'name': 'NyxMap-Companion-v1.2.3.tgz', 'state': 'uploaded', 'size': 123,
                     'browser_download_url': f'https://github.com/{REPO}/releases/download/v1.2.3/NyxMap-Companion-v1.2.3.tgz'}
        current['assets'].append(companion)
        _, urls = metadata.select_release([current], REPO)
        self.assertIn(companion['browser_download_url'], metadata.downloads(current, urls, REPO))
        rendered = metadata.downloads(current, urls, REPO)
        self.assertIn("https://raw.githubusercontent.com/bitfocus/companion-icons/main/Companion/companion.png", rendered)
        self.assertIn("alt=\"Module for Companion\"", rendered)
        self.assertNotIn("Controllable by Companion", rendered)
        self.assertIn("Setup Guide for the Companion Module", rendered)
        self.assertIn(companion['browser_download_url'], rendered)
        self.assertNotIn("bitfocus.io/companion", rendered)
        self.assertNotIn("Portable archive", rendered)
        companion['browser_download_url'] = 'https://example.org/module.tgz'
        with self.assertRaises(ValueError):
            metadata.select_release([current], REPO)

    def test_missing_duplicate_or_reversed_markers_fail_without_writes(self):
        for text in ("no markers", metadata.START * 2 + metadata.END, metadata.END + metadata.START):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "README.md").write_text(text)
                with self.assertRaises(ValueError):
                    metadata.update_files(root, [release()], REPO)
                self.assertEqual((root / "README.md").read_text(), text)
                self.assertFalse((root / "VERSION").exists())

    def cmake_version(self, tag=None, override=None, git_repo=False, release=False, exact_tag=False, uncommitted=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "cmake").mkdir()
            (root / "cmake/Version.cmake").write_text((ROOT / "cmake/Version.cmake").read_text())
            shared = root / "third_party/pxs-qt/cmake"
            shared.mkdir(parents=True)
            (shared / "PixelStereoBuildId.cmake").write_text(
                (ROOT / "third_party/pxs-qt/cmake/PixelStereoBuildId.cmake").read_text())
            (root / "VERSION").write_text("0.2.1\n")
            if git_repo:
                subprocess.run(["git", "init", "-q", str(root)], check=True)
                subprocess.run(["git", "-C", str(root), "add", "."], check=True)
                commit = ["git", "-C", str(root), "-c", "user.name=Version Test", "-c", "user.email=version@example.invalid", "commit", "-qm"]
                subprocess.run(commit + ["Release"], check=True)
                subprocess.run(["git", "-C", str(root), "tag", "v0.5.2"], check=True)
                if not exact_tag:
                    (root / "change").write_text("new build")
                    subprocess.run(["git", "-C", str(root), "add", "change"], check=True)
                    subprocess.run(commit + ["Development"], check=True)
                # the test's own files are not "changes"
                (root / ".git/info/exclude").write_text("check.cmake\nresult\n")
                if uncommitted:
                    (root / "scratch").write_text("not committed")
            script = root / "check.cmake"
            script.write_text('include("${CMAKE_CURRENT_LIST_DIR}/cmake/Version.cmake")\n'
                              'file(WRITE "${CMAKE_CURRENT_LIST_DIR}/result" "${NYXMAP_VERSION}|${NYXMAP_VERSION_NUMERIC}")\n')
            env = {**os.environ, "GITHUB_REF_TYPE": "tag" if tag else "branch", "GITHUB_REF_NAME": tag or "main"}
            cmd = ["cmake"] + ([f"-DNYXMAP_VERSION={override}"] if override else []) + (["-DNYXMAP_RELEASE_BUILD=ON"] if release else []) + ["-P", str(script)]
            result = subprocess.run(cmd, env=env, capture_output=True, text=True)
            if result.returncode:
                raise ValueError(result.stdout + result.stderr)
            return (root / "result").read_text()

    def test_tag_and_source_versions(self):
        self.assertEqual(self.cmake_version("v4.5.6"), "4.5.6|4.5.6")
        self.assertEqual(self.cmake_version("0.3"), "0.3|0.3.0")
        self.assertEqual(self.cmake_version("v1.2.3-rc.1"), "1.2.3-rc.1|1.2.3")
        self.assertEqual(self.cmake_version(), "0.2.1|0.2.1")  # no Git: nothing to count
        self.assertEqual(self.cmake_version(override="v9.8.7"), "9.8.7|9.8.7")
        self.assertEqual(self.cmake_version(override="v9.8.7", release=True), "9.8.7|9.8.7")
        with self.assertRaises(ValueError):
            self.cmake_version("vnot-a-version")

    def test_personal_build_counts_commits_since_the_release(self):
        self.assertEqual(self.cmake_version(git_repo=True), "0.5.2+1|0.5.2")
        self.assertEqual(self.cmake_version(git_repo=True, uncommitted=True), "0.5.2+1+|0.5.2")
        self.assertEqual(self.cmake_version(tag="v0.7.1", git_repo=True), "0.7.1|0.7.1")
        self.assertEqual(self.cmake_version(git_repo=True, exact_tag=True), "0.5.2|0.5.2")
        self.assertEqual(self.cmake_version(git_repo=True, exact_tag=True, uncommitted=True), "0.5.2+0+|0.5.2")


if __name__ == "__main__":
    unittest.main()
