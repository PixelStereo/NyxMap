# Limitations and remaining work

## Known problems

- **Parameter control:** competing drivers can overwrite each other without a durable takeover. See [current rules and proposed improvements](parameter-control.md).
- **Color:** compositing and FX remain in encoded sRGB, not linear light.
  Unknown/live profiles require a declaration; some video tags are unsupported.
  ISF image inputs are not individually color-managed. Some decoder/readback
  paths lose precision. Output conversion does not configure display ICC
  profiles or signal encoding; physical 10-bit output remains unverified.
- **Timing:** long stalls skip feedback/simulation steps. Physical A/V recovery and
  clock stability remain unmeasured. Input acquisition continues while paused.
- **Resources:** soft budgets miss some FFmpeg/driver/GPU allocations. Render-lock
  contention, retained decoder threads and control-event drops under load need
  measurement. Readback waits accumulate per output; slow IO can delay shutdown.
- **Media/editing:** FFT startup adds 85 ms at 48 kHz. ProRes 4444/XQ software
  decoding increases CPU load. Recall delays do not cover source/topology changes.
- **Photoshop files:** a `.psd`/`.psb` layer shows all its layers or one chosen in
  its Source tab (RGB or grayscale, 8/16-bit). All layers use the file's merged
  image when present; otherwise visible layers are composited with normal blending
  only. Other blend modes, clipping, masks, layer effects, adjustment layers and
  smart objects are not reproduced.
- **Image sequences:** a folder whose pictures (TGA, TIFF, DPX, EXR, PNG, JPEG) are
  mostly one numbered series (`name0001.exr`…) imports as one clip (Media Bin ›
  Import › Image Sequence, or drop the folder), played at the rate set in its Source
  tab (25 fps by default). FFmpeg's image2 demuxer reads a contiguous range: the
  first gap in the numbering ends the clip. Saved as `"sequence": {"pattern",
  "start", "fps"}` with the folder as the path; Collect Files copies the folder.
- **SVG:** drawings render once, on a transparent background, with their longer
  side at least the composition's (at most 8192 px); not redrawn when the layer
  is scaled. Needs Qt SVG at build time (otherwise `.svg` is not offered).
- **Network streams:** RTSP (TCP or UDP), RTMP, SRT, UDP/RTP (MPEG-TS), HTTP(S)
  progressive and HLS URLs play live (Media Bin › Import › Add Stream URL): no
  duration, no seeking or looping, the newest picture shown, no sound. A stream
  that drops or stalls (5 s without data) is connected again after a pause that
  grows to 10 s; the Source tab shows the state and the reason. Packages: macOS
  has TLS (Secure Transport) but no SRT; Linux has TLS (OpenSSL) and SRT (libsrt);
  Windows (BtbN builds) has TLS and SRT.
- **TouchDesigner:** `.tox` playback needs an installed, paid TouchDesigner/TouchPlayer on Windows/macOS. Licensed GPU playback remains to be verified on hardware. CHOP/DAT/audio are not connected; Collect Files cannot discover external component resources.
- **3D scenes:** a 3D Scene (Layer › New 3D Scene, or a model dropped onto a layer)
  is a group whose picture is its models seen by its camera, under its lights. It
  holds only 3D objects, and they stay in it: models (FBX, glTF/GLB, OBJ; read with
  ufbx and cgltf) and up to 8 lights (directional, point, spot). Each object is a
  layer: shown, soloed, animated and recalled like any other; its values are
  parameters (`model/…`, `light/…`; the scene's `camera/…`, `scene/…`). Materials:
  base color and its texture, metal, roughness, emission (no normal maps). Not yet:
  shadows, the file's animations (skeletons, keyframes) and cameras, sorting of
  transparent surfaces, layers shown on 3D surfaces.
- **Distribution:** macOS packages lack Developer ID signing and notarization.

## Roadmap

| Priority | Work | Done when |
|---|---|---|
| P1 | Profile load and fix measured bottlenecks. | 18 × 1080p layers, 60 Hz, two outputs; zero control drops/overruns. Callback p99 ≤1 ms and ≤25% of its period; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate endurance, receivers and reconnection. | Eight hours without crash/stuck transport/decoder errors; A/V offset <40 ms without growth. Twenty reconnects recover within 5 s or report an actionable error. Receiver frame IDs remain correct under load. |
| P2 | Evaluate optional linear-light compositing and verify output signaling. | Reference patches blend correctly, existing projects keep their appearance, and alpha handling and physical 10-bit precision are verified. |
| P2 | Sign and notarize macOS releases. | A downloaded release opens on a clean Mac without a Gatekeeper workaround. |

Additional proposals: [Workflow and performance ideas](ideas.md).
