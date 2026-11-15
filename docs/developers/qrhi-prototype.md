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

## 2. Video interop: next

On macOS, VideoToolbox decodes into IOSurfaces. Through a `CVMetalTextureCache`
each plane becomes an `MTLTexture`, which `QRhiTexture::createFrom` adopts
without a copy. HAP's DXT/BC blocks upload as `QRhiTexture::BC1`/`BC3`/`BC4`/`BC7`.
On Windows, D3D11VA surfaces can be adopted by the D3D11 backend. VAAPI
(dmabuf to Vulkan) is the hardest case. The prototype starts with macOS.

## 3. Cost: next

One window with a QRhi swapchain draws the same slice as OpenGL: a 4K video,
an ISF effect, an RGBA mask, one output. It is measured with the frame timing
NyxMap already records (P95/P99 intervals, missed frames, GPU memory) on the
same machine and media as `test/bench_4k.sh`.
