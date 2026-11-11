"""Check that an example project and its assets render on a software GPU."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def quarter_size(settings):
    for key in ("width", "height"):
        if settings.get(key, 0) > 0:
            settings[key] = max(1, settings[key] // 4)


def main():
    executable, source, output = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="nyxmap-example-") as folder:
        example = Path(folder) / "example"
        shutil.copytree(source.parent, example)
        fixture = example / source.name
        project = json.loads(fixture.read_text(encoding="utf-8"))
        quarter_size(project["composition"])
        for state in [project, *project.get("snapshots", [])]:
            for layer in state.get("layers", []):
                if layer.get("source", {}).get("type") == "isf":
                    quarter_size(layer["source"])
                quarter_size(layer.get("output", {}))
                params = layer.get("params", {})
                for key in ("position/x", "position/y", "width", "height"):
                    if key in params:
                        params[key] /= 4
        fixture.write_text(json.dumps(project), encoding="utf-8")
        output.unlink(missing_ok=True)
        return subprocess.call([str(executable), "--render", str(output), "--frames", "1", "--strict", str(fixture)])


if __name__ == "__main__":
    sys.exit(main())
