"""Conservative change routing: skips are explicit, nightly safety checks remain complete."""
import json
import os
import subprocess


def classify(paths, event="pull_request", ref_type="branch", full=False):
    scheduled = event == "schedule"
    release = ref_type == "tag"
    force = full or release or scheduled
    infrastructure = any(p.startswith((".github/", "cmake/")) or p in ("CMakeLists.txt", "VERSION") for p in paths)
    code = infrastructure or any(p.startswith(("src/", "test/", "third_party/", "isf/", "assets/", "scripts/", "packaging/", "licenses/")) or p in (".gitmodules", "THIRD-PARTY-NOTICES.md") for p in paths)
    engine = infrastructure or any(p.startswith(("src/engine/", "test/engine", "test/video", "test/cue_", "test/emergency", "test/recall_")) for p in paths)
    ui = infrastructure or engine or any(p.startswith(("src/ui/", "src/main", "test/")) for p in paths)
    fuzz = infrastructure or any(p.startswith(("src/engine/Hap", "src/engine/Snappy", "src/engine/OscCodec", "test/fuzz_")) for p in paths)
    result = {
        "build": (code or force) and not scheduled,
        "ui": ui or force,
        "memory": ui or engine or force,
        "tsan": engine or force,
        "fuzz": fuzz or force,
        # Allocation lifetime complements ASan, but does not need a second full workspace build on every PR.
        "valgrind": force or any(p in ("test/valgrind.supp", "test/workspace_ui.cpp") for p in paths),
    }
    # Ordinary PRs validate source builds and tests on every platform. Distribution
    # builds remain mandatory on main, on releases, and for PRs that can change them.
    package_inputs = infrastructure or any(
        p.startswith(("packaging/", "licenses/", "third_party/", "assets/")) or
        p in (".gitmodules", "THIRD-PARTY-NOTICES.md") or
        p.startswith(("scripts/package_", "scripts/smoke_", "scripts/build_ffmpeg", "scripts/build_tiff", "scripts/linux_"))
        for p in paths
    )
    result["package"] = result["build"] and (event != "pull_request" or force or package_inputs)
    result["media"] = any(result[k] for k in ("build", "ui", "tsan", "fuzz"))
    return result


def main():
    event = os.environ.get("GITHUB_EVENT_NAME", "pull_request")
    with open(os.environ["GITHUB_EVENT_PATH"]) as stream:
        payload = json.load(stream)
    base = payload.get("pull_request", {}).get("base", {}).get("sha") or payload.get("before")
    # Unknown base (first push, dispatch): validate conservatively instead of guessing that nothing changed.
    unknown = not base or not base.strip("0")
    paths = [] if unknown else subprocess.check_output(["git", "diff", "--name-only", base, "HEAD"], text=True).splitlines()
    full = unknown or os.environ.get("FULL_VALIDATION", "false").lower() == "true"
    scope = classify(paths, event, os.environ.get("GITHUB_REF_TYPE", "branch"), full)
    scope['linux'] = scope['macos'] = scope['windows'] = scope['linux_engine'] = scope['build']
    reused, previous_run = set(), None
    if event == 'pull_request' and not full:
        from ci_platform_reuse import previous_successes
        try:
            reused, previous_run = previous_successes(payload)
        except Exception as error:
            # No verified provenance: keep the normal complete checks.
            print(f'Cannot reuse previous checks ({type(error).__name__}); running the normal scope')
        for key in reused:
            scope[key] = False
        if reused:
            print(f'Reusing unchanged successful checks ({", ".join(sorted(reused))}) from {previous_run}')
    with open(os.environ["GITHUB_OUTPUT"], "a") as out:
        for key, value in scope.items():
            out.write(f"{key}={str(value).lower()}\n")
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as out:
        out.write("### Validation scope\n\n")
        if reused:
            out.write(f"\nPlatform-specific CI change; reused unchanged successful checks from {previous_run}: {', '.join(sorted(reused))}.\n\n")
        out.write("| Check | Selected |\n|---|---|\n")
        for key, value in scope.items():
            out.write(f"| {key} | {'yes' if value else 'no'} |\n")


if __name__ == "__main__":
    main()
