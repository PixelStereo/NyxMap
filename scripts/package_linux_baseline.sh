#!/bin/sh
# Executed inside the Ubuntu 24.04 builder, never against the host's libraries.
set -eu
TASK_WORK=/work
export RUNNER_TEMP="$TASK_WORK"
python3 scripts/build_ffmpeg_linux.py "$TASK_WORK/ffmpeg"
export PKG_CONFIG_PATH="$TASK_WORK/ffmpeg/lib/pkgconfig"
export LD_LIBRARY_PATH="$TASK_WORK/ffmpeg/lib"
python3 scripts/build_tiff_linux.py "$TASK_WORK/tiff-no-jbig"
export NYXMAP_TIFF_PREFIX="$TASK_WORK/tiff-no-jbig"
python3 scripts/linux_qt_plugins.py "$(/usr/lib/qt6/bin/qmake -query QT_INSTALL_PLUGINS)"
cmake -S . -B "$TASK_WORK/build" -DCMAKE_BUILD_TYPE=Release -DNYXMAP_TESTS=ON -DNYXMAP_BUNDLE_RUNTIMES=OFF
cmake --build "$TASK_WORK/build" --target NyxMap nyxmap_tests -j4
xvfb-run -a ctest --test-dir "$TASK_WORK/build" --output-on-failure --timeout 300 -R '^(scene3d|video|golden|timeline_repeat|emergency_stop|recall_integrity)$'
python3 scripts/build_omt.py "$TASK_WORK/omt"
python3 scripts/fetch_ndi.py "$TASK_WORK/ndi"
{
    cat /etc/os-release
    dpkg-query -W -f='${Package} ${Version}\n' qt6-base-dev qt6-multimedia-dev qt6-svg-dev qt6-webengine-dev libasound2-dev cmake gcc
} > deps-linux.txt
cp -a "$TASK_WORK/mediapipe" "$TASK_WORK/build/mediapipe"
python3 scripts/package_linux.py "$TASK_WORK/build" "$TASK_WORK/ffmpeg" "$TASK_WORK/omt" "$TASK_WORK/ndi" /workspace/dist
