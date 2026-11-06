#!/bin/bash
# Helpers are separate so executable resolution and pixel metrics can be checked without X11.
ui_executable() {
    python3 - "$1" <<'PY'
import json
import sys
from pathlib import Path
build = Path(sys.argv[1]).resolve()
print(build / json.loads((build / 'application.json').read_text())['executable'])
PY
}
lum() { convert "$OUT/$1.png" -crop "$2" +repage -colorspace Gray -format '%[fx:mean*255]' info:; }
same() {
    convert "$OUT/$1.png" -crop "$3" +repage "$OUT/_a.png" &&
    convert "$OUT/$2.png" -crop "$3" +repage "$OUT/_b.png" &&
    compare -metric AE "$OUT/_a.png" "$OUT/_b.png" null: 2>&1
}
