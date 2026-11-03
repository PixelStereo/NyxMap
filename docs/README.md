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
  ufbx and cgltf; or a built-in primitive from Media Bin › 3D Primitives: cube, open
  cube, plane, disc, cylinder, open cylinder, cone, pyramid, sphere, dome, torus,
  capsule, all unwrapped) and up to 8 lights (directional, point, spot). Each object is a
  layer: shown, soloed, animated and recalled like any other; its values are
  parameters (`model/…`, `light/…`; the scene's `camera/…`, `scene/…`). Materials:
  base color and its texture, metal, roughness, emission (no normal maps). Not yet:
  shadows, the file's animations (skeletons, keyframes) and cameras, sorting of
  transparent surfaces, editing a mesh's points by hand.
  A model shows another layer's picture over its UVs (its 3D tab › Surface ›
  Picture; lit, or unlit: as it is), bends across its width (`model/bend`: a curved
  screen), and has its points pushed along their normals by a layer's luminance
  (Surface › Displace; `model/displace/amount`, `model/displace/mid`). A primitive's
  mesh is cut finer with `model/detail` (1–8).
  A primitive starts with as few triangles as its shape needs (a cube is 12, a plane 2,
  a sphere 576); `model/detail` cuts it finer, and a bend or a displacement take what
  they need on their own.
  `model/shading` shows a model as made, smooth (its normals averaged where faces meet)
  or flat (its facets show). A scene shows its triangles in the layer list, its inspector
  and its 3D view, with those its last frame computed in all its passes. What the camera
  or a light does not see is not drawn, and a scene where nothing moves keeps its picture
  (not with a layer's picture on a model or around the scene, which can change at any
  frame).
  A preview cell showing a 3D scene (its source menu: "… (3D view)") is a 3D view:
  its own camera (drag: turn; Shift or middle button: pan; wheel: distance), the
  ground grid, the scene's lights and camera drawn; a click picks a model or a
  light; its handles move, turn or scale it along X, Y, Z (W, E, R; one undo step
  each). "View from Camera" and "Camera = View" exchange the view's and the scene's
  cameras. Not yet: moving a mesh's points by hand in that view.
  Lights show in that view: a lamp's head and its cone for a spot (down to the
  ground, its falloff dashed inside), a sun with parallel rays for a directional
  light, a bulb for a point light. A spot's `light/falloff` goes from a sharp edge (0)
  to a gradient from its middle (1). The models cast shadows (`light/shadow`,
  `light/softness`) from up to four spots and directional lights and two point lights,
  all around those, except what is far larger than the others (a floor, a wall), which
  only receives them; their maps are drawn again only when something they show moves.
  Without an environment, a scene's `scene/studio` gives the models a photo studio's
  panels to reflect. `scene/ao` and `scene/ao_radius` darken where surfaces meet, and
  `scene/reflections` shows the other models in smooth surfaces. Both work from what
  the camera sees: a reflection only holds what is on screen, and fades on surfaces
  that face the camera. Without a graphics card (a renderer running on the processor),
  a scene leaves out antialiasing, shadows, contact shadows and reflections. The
  Composition tab's **3D Quality** lightens all the scenes of a project when needed:
  `quality3d/antialiasing` (off, 2x, 4x, 8x), `shadows`, `contact` and `reflections`
  (off, low, medium, high). See `examples/pbr/`.
  A **Merge 3D** (Layer › New Merge 3D) is a transform for what it holds, as in a node
  graph: models, lights and other Merge 3Ds, in a 3D scene only. It is not rendered: its
  scene draws what it holds, moved, turned and scaled by `merge/position`, `rotation`
  and `scale`, its own and those above it. A hidden one hides all it holds. In the 3D
  view it is a diamond with a line to each thing it holds, and its handles move it.
  Lighting is physically based in linear light with ACES tone mapping. A scene's
  environment is a layer's picture (an HDRI, .hdr or .exr, loaded in an image layer
  that can stay hidden; or a video, a generator), equirectangular, taken at any
  stage of its FX (Scene tab › Environment): it lights the models (diffuse, and
  reflections as sharp as the surfaces are smooth) and can be shown behind them
  (`scene/environment/intensity`, `rotation`, `show`, `blur`). An HDR image layer
  (.hdr, .exr, half or float) also keeps its file as read, linear and unclipped:
  its "HDR file" stage (chosen by default) lights the scene with its full range (a
  sun far above 1), while its own picture in the composition stays clipped to 0–1
  like any layer's (8 or 16-bit normalized). Approximation: the environment's
  levels are blurred copies, not convolved. glTF and FBX materials use their metal,
  roughness, normal, occlusion and emission maps, alpha cut-out and blending
  (blended surfaces are not sorted). A primitive's material is set by
  `model/material/color`, `metal`, `rough`. Not yet: shadows.
- **Distribution:** macOS packages lack Developer ID signing and notarization.

## Roadmap

| Priority | Work | Done when |
|---|---|---|
| P1 | Profile load and fix measured bottlenecks. | 18 × 1080p layers, 60 Hz, two outputs; zero control drops/overruns. Callback p99 ≤1 ms and ≤25% of its period; lock wait p99 ≤1 ms, hold p99 ≤8 ms. |
| P1 | Validate endurance, receivers and reconnection. | Eight hours without crash/stuck transport/decoder errors; A/V offset <40 ms without growth. Twenty reconnects recover within 5 s or report an actionable error. Receiver frame IDs remain correct under load. |
| P2 | Evaluate optional linear-light compositing and verify output signaling. | Reference patches blend correctly, existing projects keep their appearance, and alpha handling and physical 10-bit precision are verified. |
| P2 | Sign and notarize macOS releases. | A downloaded release opens on a clean Mac without a Gatekeeper workaround. |

Additional proposals: [Workflow and performance ideas](ideas.md).
