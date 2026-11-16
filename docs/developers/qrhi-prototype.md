# QRhi prototype

NyxMap's renderer moves from OpenGL to Qt's QRhi, which drives Metal, Direct3D
11/12 and Vulkan from one API and keeps the application in C++ and Qt. OpenGL
goes once every part has been ported, each checked picture by picture against
the OpenGL renderer it replaces. This page records the measurements that led to
it and the parts ported so far.

## Ported

- **Video** (`RhiVideoTexture`, the port of `VideoTexture`): every pixel layout,
  HAP's compressed blocks, and hardware frames kept on the GPU
  (`RhiVideoSurface`: VideoToolbox adopted with no copy on macOS; on Windows the
  decoders decode on QRhi's Direct3D 11 device and each frame is copied once on
  the GPU, instead of the trip through the CPU the OpenGL renderer makes there).
  Planes of three channels and of four 16-bit channels, which no QRhi format
  holds, go as one channel three or four times as wide. `rhi_video` checks every
  test file against `VideoTexture`: at most 1 level apart (rounding).
- The engine's shaders are baked at build time (`qt_add_shaders`, no GLSL
  target); ISF shaders at run time (`QShaderBaker`). Qt Shader Tools is now a
  build and runtime dependency.

## Windows bench

The Windows CI job packages the benches as `NyxMap-rhi-bench-windows` (a run of
the Build workflow, `workflow_dispatch`, on the branch). On a Windows machine with
a real GPU: extract it, run `RunBench.cmd`, send back `report.txt`. It makes 4K
test videos with the bundled ffmpeg (H.264, HEVC 10-bit and HAP, as its encoders
allow), plays any video put in `media\yours`, and runs `isf_rhi`, `rhi_video` and
`rhi_composition` (4, 9 and 16 layers at 4K) through Direct3D 11 and OpenGL.

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

HAP (measured with the probe that `rhi_video` replaced) uploads HAP's compressed blocks as they are into QRhi's
block-compressed textures (BC1, BC3, BC4, BC7), decompressed by the GPU as it
samples, and converts them with the OpenGL path's formula (RGB(A), scaled
YCoCg, alpha). Per frame, upload and conversion, GPU done:

| HAP 3840×2160 | QRhi against OpenGL | QRhi Metal, median / P95 | OpenGL, median / P95 |
|---|---|---|---|
| Hap · DXT1 | identical | 0.69 / 0.83 ms | 0.74 / 0.83 ms |
| Hap Alpha · DXT5 | identical | 0.93 / 1.22 ms | 0.93 / 1.17 ms |
| Hap Q · YCoCg DXT5 | identical | 0.89 / 1.18 ms | 0.92 / 1.15 ms |

The same pictures, the same cost. One gain: on Apple Silicon OpenGL offers no
BPTC, so Hap R (BC7) and Hap HDR (BC6H) are decoded on the CPU there, while
QRhi's Metal backend samples BC7 as it is. `rhi_video` checks the
colours. HAP Q Alpha is left out of this probe (FFmpeg does not encode it); its
two textures are a BC3 and a BC4, both covered here.

Still to do: D3D11VA surfaces on Windows, VAAPI (dmabuf to Vulkan) on Linux. The probe makes
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
