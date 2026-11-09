#!/usr/bin/env python3
"""Install the local MediaPipe runtime and download versioned Google vision models."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request
import venv

BASE = "https://storage.googleapis.com/mediapipe-models/"
MODELS = {
    "hand_landmarker.task": ("hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
        "fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1"),
    "gesture_recognizer.task": ("gesture_recognizer/gesture_recognizer/float16/1/gesture_recognizer.task",
        "97952348cf6a6a4915c2ea1496b4b37ebabc50cbbf80571435643c455f2b0482"),
    "face_landmarker.task": ("face_landmarker/face_landmarker/float16/1/face_landmarker.task",
        "64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff"),
    "pose_landmarker_lite.task": ("pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
        "59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a"),
    "efficientdet_lite0.tflite": ("object_detector/efficientdet_lite0/float32/1/efficientdet_lite0.tflite",
        "40338edf5ec70d43e318b0a716a84d4564cd1802759a7a07170c7e43796dbf58"),
    "selfie_segmenter.tflite": ("image_segmenter/selfie_segmenter/float16/1/selfie_segmenter.tflite",
        "191ac9529ae506ee0beefa6b2c945a172dab9d07d1e802a290a4e4038226658b"),
    "blaze_face_short_range.tflite": ("face_detector/blaze_face_short_range/float16/1/blaze_face_short_range.tflite",
        "b4578f35940bf5a1a655214a1cce5cab13eba73c1297cd78e1a04c2380b0152f"),
    "efficientnet_lite0.tflite": ("image_classifier/efficientnet_lite0/float32/1/efficientnet_lite0.tflite",
        "6c7ab0a6e5dcbf38a8c33b960996a55a3b4300b36a018c4545801de3a3c8bde0"),
    "selfie_multiclass_256x256.tflite": ("image_segmenter/selfie_multiclass_256x256/float32/1/selfie_multiclass_256x256.tflite",
        "c6748b1253a99067ef71f7e26ca71096cd449baefa8f101900ea23016507e0e0"),
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
    for filename, (url, digest) in MODELS.items():
        destination = folder / filename
        if destination.is_file() and hashlib.sha256(destination.read_bytes()).hexdigest() == digest:
            continue
        with tempfile.NamedTemporaryFile(dir=folder, delete=False) as output:
            temporary = Path(output.name)
        try:
            checksum = hashlib.sha256()
            with urllib.request.urlopen(BASE + url, timeout=60) as response, temporary.open("wb") as output:
                while data := response.read(128 * 1024):
                    checksum.update(data)
                    output.write(data)
            if checksum.hexdigest() != digest:
                raise RuntimeError(f"Unexpected SHA-256 for {filename}")
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    subprocess.run([str(python), "-c", "from mediapipe.tasks import python"], check=True,
                   env={**os.environ, "MPLBACKEND": "Agg"})
    print(f"MediaPipe model folder: {folder}")
    print(f"Local Python: {python}")


if __name__ == "__main__":
    main()
