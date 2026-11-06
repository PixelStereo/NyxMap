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

# Xvfb is isolated to this scenario. Match the app title and choose the largest
# visible window; _NET_WM_PID is not guaranteed on every X11 backend/window.
ui_origin() {
    local best=0 w info ax ay ww hh
    for w in $(xdotool search --onlyvisible --name "$1" 2>/dev/null); do
        info=$(xwininfo -id "$w" 2>/dev/null) || continue
        ax=$(echo "$info" | awk '/Absolute upper-left X/ {print $4}')
        ay=$(echo "$info" | awk '/Absolute upper-left Y/ {print $4}')
        ww=$(echo "$info" | awk '/Width:/ {print $2}')
        hh=$(echo "$info" | awk '/Height:/ {print $2}')
        [[ "$ax" =~ ^-?[0-9]+$ && "$ay" =~ ^-?[0-9]+$ && "$ww" =~ ^[0-9]+$ && "$hh" =~ ^[0-9]+$ ]] || continue
        if [ $((ww * hh)) -gt "$best" ]; then best=$((ww * hh)); X0=$ax; Y0=$ay; fi
    done
    [ "$best" -gt 0 ]
}
ui_wait_for_window() {
    local pid=$1 deadline=$((SECONDS + $2)) title=$3
    until ui_origin "$title"; do
        kill -0 "$pid" 2>/dev/null || { echo "Application exited during startup" >&2; return 1; }
        [ "$SECONDS" -lt "$deadline" ] || { echo "Timed out waiting for application window" >&2; return 1; }
        sleep 0.2
    done
}
