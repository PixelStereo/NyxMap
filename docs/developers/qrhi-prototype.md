# QRhi prototype

The renderer is OpenGL. OpenGL is deprecated on macOS, so NyxMap studies a move to
Qt's QRhi, which drives Metal, Direct3D 11/12 and Vulkan from one API and keeps
the application in C++ and Qt. Nothing changes in the production renderer until
a measured prototype shows a gain without losing a feature.

The prototype answers three questions, in this order:

1. **Shaders.** Can every ISF shader run on Metal, Direct3D and Vulkan?
2. **Video interop.** Do VideoToolbox surfaces (and HAP's compressed textures)
   reach a QRhi texture without a copy through the CPU?
3. **Cost.** Is a real composition (video, ISF effect, RGBA mask, output) at
   least as fast and as steady as with OpenGL?

## 1. Shaders: answered

`IsfInstance::rhiSources` writes a shader as Vulkan-flavoured GLSL 440: its
values in one std140 block, each image at its own binding, the varyings at
explicit locations matched by name between the stages, `gl_VertexID` as
`gl_VertexIndex`. A built-in function the shader defines again (`round`…) is
renamed: OpenGL takes it as an overload, Vulkan's compiler refuses it.
`QShaderBaker` (glslang, then SPIRV-Cross) bakes the result into SPIR-V 1.0,
MSL 2.0 and HLSL 5.0. No GPU is needed.

```bash
build/nyxmap_isf_rhi_test --verbose [folders or .fs files]
```

Result (Qt 6.12): **all 311 shipped shaders** bake for all three backends, in
0.6 s. The `isf_rhi` test fails if a shipped shader stops baking.

Not covered yet: the generated code has not run on a GPU. Multi-pass targets,
persistent buffers, POINTS passes and image flips are translated but not drawn,
and QRhi's clip space and texture origin differ from OpenGL's (handled with
`QRhi::isYUpInFramebuffer` and `isYUpInNDC` when drawing).

## 2. Video interop: answered on macOS

`test/rhi_video.mm` hands VideoToolbox's frames to an offscreen Metal QRhi with
no copy: a `CVMetalTextureCache` makes each plane of the frame's IOSurface an
`MTLTexture`, which `QRhiTexture::createFrom` adopts. One draw converts it to
RGBA with the OpenGL path's formula (`VideoLayout`: channels, bit depth, chroma
siting, matrix, range). The same frames also go through the OpenGL path
(`VideoTexture::upload`), to compare.

```bash
build/nyxmap_rhi_video_test [--frames N] [video files...]
```

Results (Apple M4 Max, Qt 6.12), per frame with the GPU done:

| Video | QRhi against OpenGL | QRhi Metal, median / P95 | OpenGL, median / P95 |
|---|---|---|---|
| H.264 1280×720 NV12 | max 1, mean 0.12 (rounding) | 0.21 / 0.77 ms | 0.24 / 1.91 ms |
| H.264 3840×2160 NV12 | identical | 0.83 / 2.56 ms | 0.75 / 2.46 ms |
| HEVC 10-bit 3840×2160 P010 | identical | 0.29 / 1.02 ms | 0.36 / 0.82 ms |

The pictures are the OpenGL path's, and as close to FFmpeg's own decoding. The
cost is the same order: neither API is the bottleneck for one stream. The
`rhi_video` test checks the colours (skipped without Metal or hardware frames).

Still to do: HAP's compressed textures (`QRhiTexture::BC1`/`BC3`/`BC4`/`BC7`),
D3D11VA surfaces on Windows, VAAPI (dmabuf to Vulkan) on Linux. The probe makes
the planes' QRhi textures again for each frame; a cache by surface would lower
its cost.

## 3. Cost of a whole composition: answered on macOS

`test/rhi_composition.mm` renders the same composition through both APIs: each
layer a video (hardware frames, no copy), converted, an ISF effect, drawn into
its cell of a grid, multiplied by an RGBA mask, blended over the others. OpenGL
runs the engine's own code (`VideoTexture`, `IsfInstance::render`); QRhi runs
`IsfInstance::rhiSources`'s shaders, with the uniform block it describes. Each
frame is waited for (GPU done); the videos play in real time.

```bash
build/nyxmap_rhi_composition_test [--layers N] [--seconds S] [--size WxH] [--effect file.fs] [--opengl-first] [video]
```

The first frame matches between the two (max 1 or 2 levels in 255, rounding):
the ISF shaders run correctly on the GPU through QRhi. The `rhi_composition`
test checks it. Metal and Direct3D draw upside down (`isf_ndcFlip`), so their
textures hold the picture's bottom first, as OpenGL's do, and the shaders sample
them unchanged.

Results (Apple M4 Max, Qt 6.12), 4K output, Kaleidoscope on every layer:

| Layers of 4K video | QRhi Metal, median / P95 / P99 | OpenGL, median / P95 / P99 | QRhi faster |
|---|---|---|---|
| 4 × H.264 | 0.68 / 0.95 / 1.29 ms | 1.63 / 3.54 / 3.80 ms | 2.4× |
| 9 × H.264 | 1.57 / 2.90 / 3.36 ms | 2.82 / 5.62 / 5.87 ms | 1.8× |
| 9 × HEVC 10-bit | 1.55 / 3.03 / 3.28 ms | 2.75 / 5.99 / 6.31 ms | 1.8× |
| 16 × H.264 | 2.68 / 2.95 / 3.31 ms | 6.52 / 7.10 / 8.49 ms | 2.4× |

Running OpenGL first gives the same figures. At 16 layers the decoders, not the
GPU, set the pace (41 video frames/s per layer through both APIs).

On macOS, QRhi through Metal takes about half the GPU time of OpenGL for the
same work, with tails half as long. OpenGL there is a layer over Metal, which
NyxMap pays for at every frame.

Not measured yet: a swapchain and presentation (vsync, several outputs),
multi-pass and persistent ISF, HAP, Windows (Direct3D 11/12) and Linux (Vulkan).
