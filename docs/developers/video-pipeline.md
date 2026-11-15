# Video upload and conversion

The renderer remains OpenGL on Windows and macOS. Changing the graphics API is
not required for these optimizations.

* VideoToolbox surfaces on macOS keep their direct IOSurface sampling path.
* When a hardware surface cannot be mapped for CPU reads, the decoder tries a
  synchronous FFmpeg download directly into a mapped OpenGL upload buffer. This
  removes the intermediate software picture and the subsequent plane-packing
  copy. It applies only when the hardware allocation matches the visible image,
  the pixel layout is supported, and an upload buffer is available. Padded
  allocations, rejected transfers and exhausted upload pools use the original
  download path. This is a readback optimization, not Windows zero-copy interop.
* A surface already mapped for CPU reads is packed from that mapping when the
  upload pool is exhausted, rather than downloaded from hardware a second time.
* Conversion binds each physical plane once. Packed RGB, gray and packed 4:4:4
  share one texture lookup across channels. Interleaved chroma formats such as
  NV12, NV21 and P010 share the UV lookup. Channel selection, alpha, chroma siting,
  range, matrix and bit-depth scaling remain common to all formats.

## Memory budget

`VideoBudget` arbitrates what every stream may hold, in one place.

* **Video memory envelope:** a quarter of the RAM, at most 2.5 GiB
  (`NYXMAP_VIDEO_MEMORY_MB` overrides it). A fifth goes to the decoded frames
  kept in the decoders' memory (a soft limit: over it, every stream keeps fewer
  frames ahead, none is dropped), the rest to the upload buffers.
* **Upload buffers:** a hard limit. A buffer is reserved before it is made; the
  layers wanting buffers share the limit evenly, a newcomer getting its share as
  the others give theirs back. A layer without room uploads from the decoder's
  memory (the slow path) instead of going over.
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

`nyxmap_tests --area video` compares GPU output against FFmpeg for planar,
semiplanar and packed layouts, including alpha and high bit depths, and checks
HAP and upload-buffer playback. Both ordinary and rectangle conversion shaders
are compiled. On Linux, `video_transfer` substitutes the hardware transfer call
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
Software OpenGL tests establish rendering correctness, not equal performance
between different machines or validation of native hardware transfers.
