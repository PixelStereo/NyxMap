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

## 3. Cost of a whole composition: next

One window with a QRhi swapchain draws the same slice as OpenGL: several 4K
videos, an ISF effect, an RGBA mask, one output. It is measured with the frame
timing NyxMap already records (P95/P99 intervals, missed frames, GPU memory),
on the same machine and media as `test/bench_4k.sh`. That, more than one
stream's conversion, decides whether the move is worth it.
