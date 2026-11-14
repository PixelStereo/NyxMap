# Motion masks and visual interactions

## Measure motion from a camera

1. Add a camera source to a layer. The same tools work on movies, images, and generators.
2. Add **Motion Difference Absolute** or **Motion Difference Relative** from the effects library.
3. Adjust **Threshold** and **Softness** while watching the black/white image. White means change. Use the ordinary FX mask routing when this image should mask another effect.
4. In **Interactions**, add a **Pixels** input. Select the analyzed layer and **Post FX** to measure the resulting mask. Select **Active area** for the fraction of changed pixels, **Mean / amount** for average intensity, or **Energy** for mean squared intensity.
5. Map that input to an effect parameter, then adjust the existing interaction range, curve and smoothing controls.

A hidden layer can still supply an enabled visual interaction. Select a layer and choose **Pre FX** or **Post FX**. Post FX reads the complete effect chain, including a generated motion mask. Pre FX reads that layer before its effects. Composition and raw-source taps are not offered; older raw-source taps migrate to Pre FX. A source layer must be selected before analysis starts.

### Absolute reference

**Motion Difference Absolute** captures the initial image. Press **Capture reference** with an empty scene to detect a person entering or leaving. An optional external reference image can be selected instead. **Background adaptation** slowly learns quiet pixels; leave it at zero for a fixed reference. It does not learn pixels above the change threshold.

**Lighting compensation** offers off, global RGB offset, and offset plus exposure compensation. Leave it off to detect lighting changes. Offset compensation removes common brightness/color shifts; exposure compensation also adjusts contrast when the reference contains sufficient contrast. Statistics use a coarse median, so a small foreground object does not shift the whole background. Large objects covering most of the scene, shadows, clipped highlights and spatially varying illumination can still produce changes. This is image differencing, not semantic person segmentation.

### Relative history

**Motion Difference Relative** compares the current frame with the frame **N** rendered frames ago, or with the mean of the previous **N** frames (1–8). Press **Reset history** after changing a camera or scene. Initialization and reset fill the complete history and suppress the initial flash.

Both effects also offer **Lighten accumulation (hold)** under temporal smoothing. **Lighten hold frames** supports 1–4096 frames, with 0 retaining the result until Capture reference / Reset history. Each changed pixel retains its brightest mask value until that many frames without new motion at that pixel. Binary masks therefore show the union of recently moving regions. Graded masks hold their peak and refresh the hold on each new hit; this is a peak hold rather than an exact sliding maximum of graded values. Use Active area or Energy in a separate Post FX pixel interaction to measure the accumulated footprint. Threshold and blur still apply before accumulation. The comparison history remains independent from this longer accumulation window.

Both effects offer spatial blur and temporal persistence. **Maximum trail** retains recent movement; **Exponential average** smooths it. These controls affect the mask itself. The interaction's filters can separately smooth a derived number. History and adaptation follow rendered frames, so their timing depends on frame rate.

## Independent pixel analysis

The **Pixels** source can measure any image without either motion FX. Channels are red, green, blue, alpha, luminance (Rec.709 weights), and chrominance magnitude (distance from luminance). Select a normalized region using X, Y, width and height; the preview outlines it.

| Measurement | Meaning |
|---|---|
| Mean / amount | Average selected-channel value, 0–1 |
| Energy | Mean squared selected-channel value, 0–1 |
| Active area | Fraction above the threshold, 0–1 |
| Peak | Maximum selected-channel value |
| Centroid X / Y | Intensity-weighted center of pixels above threshold, in normalized image coordinates |
| Spread | Weighted distance from that center |
| Frame change | Average absolute difference from the last analyzed image |
| Velocity X / Y | Centroid displacement per second; may be negative |
| Mean derivative | Change in mean intensity per second; may be negative |

Pixel analysis uses asynchronous GPU readback at 64×64. It drops samples when the GPU is busy. These values are suitable for interactive control, not full-resolution measurement. Up to eight distinct source/tap pairs and 128 analyzer configurations are supported. Missing images or data older than half a second yield zero. The first sample and configuration changes reset temporal measurements.

## MediaPipe Tasks

Release packages contain a portable Python runtime and the verified models. In
**Interactions → MediaPipe**, select a camera, video or image layer. The bundled worker
runs locally without installing Python or downloading models at first launch.

For source builds and CI inference tests, use
`python3 scripts/setup_mediapipe.py --directory /path/to/mediapipe-models`.
To use an existing Python environment, set `NYXMAP_MEDIAPIPE_PYTHON` to its interpreter
before launching NyxMap. The supported wheels cover macOS Apple Silicon, Linux x86-64,
and Windows x86-64.

| Task | Available interaction values |
|---|---|
| Hand landmarks | Presence, handedness confidence, wrist/index/thumb position, pinch, openness, rotation, count |
| Gesture Recognizer | Fist, open palm, pointing up, thumbs up/down, victory and I love you |
| Face Landmarker | Count, position, jaw opening and left/right eye blinks |
| Pose Landmarker | Count and nose/left wrist/right wrist position |
| Object Detector | Count, box center, width, height and confidence |
| Face Detector | Count, box center, width, height and confidence |
| Image Classifier | Label in the preview, confidence and class index as interaction values |
| Image Segmenter | Foreground coverage, mask overlay and a compositing layer source |

Normalized image coordinates have Y increasing downward. Each selected measurement can drive a parameter,
an action condition, or an XY input. The detection selector exposes the first two detections.
Only requested tasks run for a source. Readback is asynchronous and frames are submitted at up to 15 Hz
at the source texture’s native dimensions, without a separate resolution cap. The queue keeps the latest pending frame per source, and stale detections
yield zero after half a second without a completed result. Tasks use image mode and try the GPU delegate first, then use the CPU when the GPU delegate fails.
The interaction status displays the active delegate. Set `NYXMAP_MEDIAPIPE_DELEGATE=cpu` to force CPU,
or `NYXMAP_MEDIAPIPE_DELEGATE=gpu` to require GPU. The dedicated hair model may require CPU on Metal. Rerun setup to install the float32 detector and the new models. Stable wrist slots are maintained in NyxMap.
Face, pose and object indices follow model order. To use the segmentation in a composition, create a layer and choose **Source → From layer**.
Select the video layer and set **Taken → MediaPipe Foreground Mask**. The mask uses the model’s confidence as white alpha and can be used as a layer matte. **MediaPipe Hair Mask** isolates hair, and
**MediaPipe Landscape Mask** uses the wide selfie model, **MediaPipe Detailed Hair Mask** uses the dedicated hair model,
**MediaPipe DeepLab Foreground** masks all recognized non-background classes, and **MediaPipe Multiclass Foreground**
uses the background confidence from the multiclass selfie model. **MediaPipe Body Parts** gives a colored texture for hair, body skin, face skin, clothes and accessories. **MediaPipe Person Cutout** applies the confidence map to the exact sampled source frame. It is synchronized and keeps the sampled source dimensions. Each model still has its own internal inference grid. When applying the mask to the live source separately, account for inference latency.
**MediaPipe Pose Skeleton** renders the body as a transparent texture with OpenPose COCO-18 joint order. Indexed measurements expose the normalized hand, face and pose landmarks,
world coordinates for hands and pose, pose landmark visibility, face blendshape scores, facial transformation matrix components and left/right handedness scores.

Existing hand mappings keep their feature and source IDs. Replace the former ONNX folder with the new model folder.
Model errors are shown in the interaction. Pixel analysis and motion masks remain available independently.

Run the real SDK tests after installation with
`NYXMAP_HAND_MODELS=/path/to/mediapipe-models NYXMAP_MEDIAPIPE_ALL_MODELS=1 ctest --test-dir build -R vision_hand_inference --output-on-failure`.
Set `NYXMAP_HAND_IMAGE` to a hand image to also check positive detection. Without models the inference test skips.
See the [official MediaPipe Tasks documentation](https://developers.google.com/edge/mediapipe/solutions/tasks).
