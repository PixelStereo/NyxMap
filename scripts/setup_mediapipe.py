#!/usr/bin/env python3
"""Install the local MediaPipe runtime and download versioned Google vision models."""
import argparse
import os
from pathlib import Path
import subprocess
import urllib.request
import venv

BASE = "https://storage.googleapis.com/mediapipe-models/"
MODELS = {
    "hand_landmarker.task": "hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
    "gesture_recognizer.task": "gesture_recognizer/gesture_recognizer/float16/1/gesture_recognizer.task",
    "face_landmarker.task": "face_landmarker/face_landmarker/float16/1/face_landmarker.task",
    "pose_landmarker_lite.task": "pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
    "efficientdet_lite0.tflite": "object_detector/efficientdet_lite0/int8/1/efficientdet_lite0.tflite",
    "selfie_segmenter.tflite": "image_segmenter/selfie_segmenter/float16/1/selfie_segmenter.tflite",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    folder = args.directory.expanduser().resolve()
    folder.mkdir(parents=True, exist_ok=True)
    runtime = folder / ".venv"
    venv.EnvBuilder(with_pip=True, symlinks=os.name != "nt").create(runtime)
    python = runtime / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    subprocess.run([str(python), "-m", "pip", "install", "mediapipe==0.10.32"], check=True)
    for filename, url in MODELS.items():
        destination = folder / filename
        if destination.exists():
            continue
        temporary = destination.with_suffix(".download")
        try:
            with urllib.request.urlopen(BASE + url, timeout=60) as response, temporary.open("wb") as output:
                while data := response.read(128 * 1024):
                    output.write(data)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    print(f"MediaPipe model folder: {folder}")
    print(f"Local Python: {python}")


if __name__ == "__main__":
    main()
