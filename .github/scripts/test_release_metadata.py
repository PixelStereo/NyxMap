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
    names = [f"NyxMap-macos-{tag}.zip", f"NyxMap-windows-{tag}.zip",
             f"NyxMap-linux-x86_64-{tag}.AppImage", f"NyxMap-linux-x86_64-{tag}.tar.gz"]
    return {"tag_name": tag, "published_at": date, "draft": False, "prerelease": False, **flags,
            "assets": [{"name": name, "state": "uploaded", "size": 123,
                        "browser_download_url": f"https://github.com/{REPO}/releases/download/{tag}/{name}"}
                       for name in names]}


class ReleaseMetadataTests(unittest.TestCase):
    def test_actual_assets_and_idempotence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = f"before\n{metadata.START}\nstale\n{metadata.END}\nafter\n"
            (root / "README.md").write_text(original, encoding="utf-8")
            current = release()
            metadata.update_files(root, [current], REPO)
            text = (root / "README.md").read_text(encoding="utf-8")
            self.assertTrue(text.startswith("before\n") and text.endswith("\nafter\n"))
            for asset in current["assets"]:
                self.assertIn(asset["browser_download_url"], text)
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
        self.assertIn("(preview)", metadata.downloads(chosen, urls, REPO))
        self.assertIn("1.3.0-rc.1", metadata.downloads(chosen, urls, REPO))

    def test_bad_urls_and_no_complete_release_fail(self):
        bad = release()
        bad["assets"][0]["browser_download_url"] = "https://example.org/package.zip"
        with self.assertRaises(ValueError):
            metadata.select_release([bad], REPO)
        with self.assertRaises(ValueError):
            metadata.select_release([], REPO)

    def test_remote_link_only_when_asset_is_published(self):
        current = release()
        _, urls = metadata.select_release([current], REPO)
        self.assertNotIn('Download Open Stage Control remote', metadata.downloads(current, urls, REPO))
        remote = {'name': 'NyxMap-OpenStageControl.zip', 'state': 'uploaded', 'size': 123,
                  'browser_download_url': f'https://github.com/{REPO}/releases/download/v1.2.3/NyxMap-OpenStageControl.zip'}
        current['assets'].append(remote)
        _, urls = metadata.select_release([current], REPO)
        self.assertIn(remote['browser_download_url'], metadata.downloads(current, urls, REPO))
        remote['browser_download_url'] = 'https://example.org/remote.zip'
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

    def cmake_version(self, tag=None, override=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "cmake").mkdir()
            (root / "cmake/Version.cmake").write_text((ROOT / "cmake/Version.cmake").read_text())
            (root / "VERSION").write_text("0.2.1\n")
            script = root / "check.cmake"
            script.write_text('include("${CMAKE_CURRENT_LIST_DIR}/cmake/Version.cmake")\n'
                              'file(WRITE "${CMAKE_CURRENT_LIST_DIR}/result" "${NYXMAP_VERSION}|${NYXMAP_VERSION_NUMERIC}")\n')
            env = {**os.environ, "GITHUB_REF_TYPE": "tag" if tag else "branch", "GITHUB_REF_NAME": tag or "main"}
            cmd = ["cmake"] + ([f"-DNYXMAP_VERSION={override}"] if override else []) + ["-P", str(script)]
            result = subprocess.run(cmd, env=env, capture_output=True, text=True)
            if result.returncode:
                raise ValueError(result.stdout + result.stderr)
            return (root / "result").read_text()

    def test_tag_and_source_versions(self):
        self.assertEqual(self.cmake_version("v4.5.6"), "4.5.6|4.5.6")
        self.assertEqual(self.cmake_version("0.3"), "0.3|0.3.0")
        self.assertEqual(self.cmake_version("v1.2.3-rc.1"), "1.2.3-rc.1|1.2.3")
        self.assertEqual(self.cmake_version(), "0.2.1|0.2.1")
        self.assertEqual(self.cmake_version(override="v9.8.7"), "9.8.7|9.8.7")
        with self.assertRaises(ValueError):
            self.cmake_version("vnot-a-version")


if __name__ == "__main__":
    unittest.main()
