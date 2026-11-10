# Media sources

Media files, folders and streams go into the **Media Bin**, then onto a layer. This page covers the formats that need explanation; video playback internals are in [Video pipeline](../developers/video-pipeline.md).

## Photoshop files

A `.psd` / `.psb` layer shows all its layers, or one chosen in its **Source** tab (RGB or grayscale, 8/16-bit). **All layers** uses the file's merged image when present; otherwise visible layers are composited with normal blending only.

!!! note "Not reproduced"
    Other blend modes, clipping, masks, layer effects, adjustment layers and smart objects.

## Image sequences

A folder whose pictures (TGA, TIFF, DPX, EXR, PNG, JPEG) are mostly one numbered series (`name0001.exr`…) imports as one clip: **Media Bin › Import › Image Sequence**, or drop the folder. It plays at the rate set in its **Source** tab (25 fps by default).

FFmpeg's image2 demuxer reads a contiguous range: the first gap in the numbering ends the clip. The project saves it as `"sequence": {"pattern", "start", "fps"}` with the folder as the path; **Collect Files** copies the folder.

## SVG drawings

Drawings render once, on a transparent background, with their longer side at least the composition's (at most 8192 px). They are not redrawn when the layer is scaled. SVG needs Qt SVG at build time; otherwise `.svg` is not offered.

## Network streams

RTSP (TCP or UDP), RTMP, SRT, UDP/RTP (MPEG-TS), HTTP(S) progressive and HLS URLs play live: **Media Bin › Import › Add Stream URL**.

- No duration, seeking or looping; the newest picture is shown; no sound.
- A stream that drops or stalls (5 s without data) is connected again after a pause that grows to 10 s. The **Source** tab shows the state and the reason.

| Package | TLS | SRT |
|---|---|---|
| macOS | Secure Transport | — |
| Linux | OpenSSL | libsrt |
| Windows | yes | yes |
