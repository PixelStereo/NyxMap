#pragma once
// The process-wide budget of the video streams (any thread): one place arbitrating what every stream may hold.
//
// Memory: one envelope for the frames waiting to be shown in host memory, by default a quarter of the machine's RAM
// up to 2.5 GiB (NYXMAP_VIDEO_MEMORY_MB overrides it). A fifth of it goes to the decoded frames kept in the
// decoders' memory, the rest to the upload buffers the GPU copies from:
//  - decoded frames: a soft limit. Going over it never drops a frame, it makes every stream keep fewer ahead.
//  - upload buffers: a hard limit. A buffer is reserved before it is allocated; refused, the stream's frames go
//    through the decoders' memory (the slow path, counted with the decoded frames). The layers wanting buffers
//    share the limit evenly: a newcomer's share is made as the others give back their extra buffers.
//  - byte buffers kept for reuse by the decoders: a hard limit of their own, counted with the decoded frames.
// Not counted: hardware surfaces (the decoder's pool, on the GPU), textures, FFmpeg's internal allocations.
//
// Threads: FFmpeg's threads of the software decoders. Reserved at once with the decoder's count, under one lock:
// two decoders opening together never see the same room. A soft limit: beyond it a new decoder gets an even share
// (at least 2); those already open keep theirs (set when FFmpeg opens).
#include <cstddef>

namespace videoBudget {

// Envelope
size_t memoryLimit();
void setMemoryLimit(size_t bytes); // 0: the default (tests, measurements); the shares follow
size_t memoryInUse();              // decoded frames + upload buffers

// Decoded frames (and the decoders' pooled byte buffers)
void addDecoded(size_t bytes);
void removeDecoded(size_t bytes);
size_t decodedInUse();
size_t decodedLimit();
bool decodedOver(); // more than its share is kept: queues keep their minimum
void setDecodedLimit(size_t bytes); // 0: its share of the envelope (tests)

bool reservePooled(size_t bytes); // false: over the pool's limit, the buffer is freed instead
void releasePooled(size_t bytes);

// Upload buffers
bool reserveUpload(size_t bytes); // false: no room, the buffer is not made
void releaseUpload(size_t bytes);
size_t uploadInUse();
size_t uploadLimit();
void addUploadStream();    // a layer wanting upload buffers
void removeUploadStream();
int uploadStreams();
size_t uploadShare(); // what each layer wanting buffers may hold

// Software decoders' threads
int reserveThreads(int alone); // the threads a new decoder gets (alone: what it would get alone), reserved
void releaseThreads(int threads);
int threadsInUse();
int decodersOpen();
int threadLimit();
void setThreadLimit(int threads); // 0: the default (twice the cores), tests and measurements

// Graphics memory NyxMap allocated (any thread), estimated from the sizes and formats asked for: a report, not a
// limit. Render targets: every framebuffer texture (layers, effects, outputs, multisampling). Images: textures of
// still pictures and data. Video planes: the textures a video frame is uploaded to. Surfaces: the hardware
// decoders' frames NyxMap holds (queued, shown, bound), sampled where they were decoded.
enum class Gpu { Targets, Images, VideoPlanes, Surfaces, Count };
void addGpu(Gpu kind, size_t bytes);
void removeGpu(Gpu kind, size_t bytes);
size_t gpuInUse(Gpu kind);
size_t gpuInUse(); // all of them

// The machine's physical memory (0 when the system does not tell)
size_t physicalMemory();

}
