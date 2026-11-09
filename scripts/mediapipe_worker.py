"""Local MediaPipe Tasks worker. Stdout is reserved for one JSON reply per frame."""
import json
import sys
from pathlib import Path

MODELS = {
    "hands": ("HandLandmarker", "hand_landmarker.task"),
    "gestures": ("GestureRecognizer", "gesture_recognizer.task"),
    "face": ("FaceLandmarker", "face_landmarker.task"),
    "pose": ("PoseLandmarker", "pose_landmarker_lite.task"),
    "objects": ("ObjectDetector", "efficientdet_lite0.tflite"),
    "segment": ("ImageSegmenter", "selfie_segmenter.tflite"),
}


def reply(value):
    print(json.dumps(value, separators=(",", ":"), allow_nan=False), flush=True)


def run(folder):
    import mediapipe as mp
    import numpy as np

    vision = mp.tasks.vision
    tasks = {}

    def points(landmarks):
        return [[p.x, p.y, p.z] for p in landmarks]

    def infer(task, image):
        if task not in tasks:
            class_name, filename = MODELS[task]
            options = {"base_options": mp.tasks.BaseOptions(model_asset_path=str(folder / filename))}
            if task in ("hands", "gestures"):
                options.update(num_hands=2, min_hand_detection_confidence=.5)
            elif task == "face":
                options.update(num_faces=2, output_face_blendshapes=True)
            elif task == "pose":
                options.update(num_poses=2)
            elif task == "objects":
                options.update(max_results=10, score_threshold=.5)
            elif task == "segment":
                options.update(output_category_mask=True, output_confidence_masks=False)
            tasks[task] = getattr(vision, class_name).create_from_options(getattr(vision, class_name + "Options")(**options))
        detector = tasks[task]
        if task in ("hands", "gestures"):
            result = detector.detect(image) if task == "hands" else detector.recognize(image)
            return {"hands": [
                {"points": points(hand), "score": result.handedness[i][0].score,
                 "right": result.handedness[i][0].category_name == "Right",
                 "gesture": result.gestures[i][0].category_name
                 if task == "gestures" and result.gestures[i] else ""}
                for i, hand in enumerate(result.hand_landmarks)
            ]}
        if task == "face":
            result = detector.detect(image)
            return {"face": [
                {"points": points(face), "expressions": {p.category_name: p.score for p in result.face_blendshapes[i]}}
                for i, face in enumerate(result.face_landmarks)
            ]}
        if task == "pose":
            return {"pose": [{"points": points(pose)} for pose in detector.detect(image).pose_landmarks]}
        if task == "objects":
            return {"objects": [
                {"box": [d.bounding_box.origin_x / image.width, d.bounding_box.origin_y / image.height,
                         d.bounding_box.width / image.width, d.bounding_box.height / image.height],
                 "score": d.categories[0].score, "label": d.categories[0].category_name}
                for d in detector.detect(image).detections if d.categories
            ]}
        mask = detector.segment(image).category_mask.numpy_view()
        return {"coverage": float(np.count_nonzero(mask) / mask.size)}

    reply({"ready": True})
    for line in sys.stdin.buffer:
        try:
            width, height, mask = map(int, line.split())
            if not (0 < width <= 2048 and 0 < height <= 2048):
                raise ValueError("Invalid frame size")
            data = sys.stdin.buffer.read(width * height * 4)
            if len(data) != width * height * 4:
                break
            rgba = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgba[:, :, :3]))
            result = {}
            for bit, task in enumerate(MODELS):
                if mask & (1 << bit):
                    try:
                        result.update(infer(task, image))
                    except Exception as error:
                        result[task + "_error"] = str(error)
            reply(result)
        except Exception as error:
            reply({"error": str(error)})


if __name__ == "__main__":
    try:
        run(Path(sys.argv[1]))
    except Exception as error:
        reply({"error": str(error)})
