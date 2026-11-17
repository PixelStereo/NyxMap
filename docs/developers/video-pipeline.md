# Video upload and conversion

Frames are decoded into the decoders' memory (FFmpeg, on their own threads) and uploaded by the QRhi renderer
(`RhiVideoTexture`), on Metal, Direct3D 11 and Vulkan alike.

* VideoToolbox surfaces on macOS are sampled in place, as Metal textures of QRhi's device (no copy, no upload).
* On Windows, Direct3D 11 hardware frames are copied on the GPU into a texture QRhi samples.
* Anything else is uploaded from memory: its planes become textures, and one draw converts them into the RGBA picture the
  layer samples (RGBA8, or RGBA16F with the 10-bit precision). Packed RGB, gray and packed 4:4:4 share one texture lookup
  across channels; interleaved chroma formats such as NV12, NV21 and P010 share the UV lookup. Channel selection, alpha,
  chroma siting, range, matrix and bit-depth scaling are common to all formats.
* HAP's compressed textures (DXT, BPTC) go to the GPU as they are when the device samples them, else the decoder expands
  them on the CPU.

## Memory budget

`VideoBudget` arbitrates what every stream may hold, in one place.

* **Video memory envelope:** a quarter of the RAM, at most 2.5 GiB
  (`NYXMAP_VIDEO_MEMORY_MB` overrides it). A fifth goes to the decoded frames
  kept in the decoders' memory (a soft limit: over it, every stream keeps fewer
  frames ahead, none is dropped).
* **Decode threads:** reserved under one lock before FFmpeg opens the decoder
  (`NYXMAP_DECODE_THREAD_BUDGET`, twice the cores by default). Beyond the limit,
  a new decoder gets an even share, at least two.
* **Graphics memory (reported, not limited):** render targets, images, video
  plane textures and the hardware surfaces NyxMap holds, estimated from their
  sizes and formats.

`/composition/budget` (OSC) returns all of it as JSON, the status bar shows the
graphics memory and the video envelope (amber when a budget is exceeded, the
detail when hovered), and the endurance CSV logs the graphics memory allocated.

## Validation

`nyxmap_tests --area video` compares QRhi's output against FFmpeg's swscale for planar,
semiplanar and packed layouts, including alpha and high bit depths, and checks
HAP playback and the graphics memory report. On Linux, `video_transfer` substitutes the hardware transfer call
to check borrowed buffer ownership, plane addresses/strides, allocation guards,
failure cleanup and rejection of padded surfaces without requiring a decoder GPU.

Native performance must be measured on both platforms. Use the same media and
output configuration before and after the change; record decoded/displayed
frames, lateness, decode P95/P99, CPU/GPU time and memory. The existing
`test/bench_4k.sh` exercises H.264, HEVC10, ProRes and HAP at increasing layer
counts using `nyxmap_bench`; it requires FFmpeg and a POSIX shell. On Windows,
run `nyxmap_bench.exe --seconds 6 --fps 60 --comp 3840x2160 <files...>` with the
same media and layer counts, with and without `--copy`.
Also run `nyxmap_tests --area video` on Windows and macOS with native GPU drivers.
Tests on a software GPU (WARP, lavapipe) establish rendering correctness, not equal performance
between different machines or validation of native hardware transfers.
