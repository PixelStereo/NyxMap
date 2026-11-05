"""Qt plugins required by both the Linux packager and its runtime smoke test."""

REQUIRED_PLUGIN_GROUPS = (
    ("platforms/libqxcb.so",),
    # Qt 6.10 consolidates the older Wayland platform plugins into this one.
    ("platforms/libqwayland.so", "platforms/libqwayland-egl.so"),
    ("multimedia/libgstreamermediaplugin.so", "multimedia/libffmpegmediaplugin.so"),
)


def missing_plugin_groups(plugin_root):
    return [" or ".join(group) for group in REQUIRED_PLUGIN_GROUPS
            if not any((plugin_root / name).is_file() for name in group)]


def multimedia_backend(plugin_root):
    for backend in ("gstreamer", "ffmpeg"):
        if (plugin_root / "multimedia" / f"lib{backend}mediaplugin.so").is_file():
            return backend
    return None


if __name__ == "__main__":
    import sys
    from pathlib import Path

    root = Path(sys.argv[1])
    missing = missing_plugin_groups(root)
    if missing:
        sys.exit("Missing required Qt plugins: " + "; ".join(missing)
                 + "\nAvailable: " + ", ".join(str(path.relative_to(root)) for path in root.rglob("*.so")))
    print(f"Required Linux Qt plugins present in {root}")
