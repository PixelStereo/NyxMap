#include "VideoBudget.h"

#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

namespace videoBudget {

size_t physicalMemory()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX s{};
    s.dwLength = sizeof(s);
    return GlobalMemoryStatusEx(&s) ? size_t(s.ullTotalPhys) : 0;
#elif defined(Q_OS_MACOS)
    uint64_t bytes = 0;
    size_t len = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0 ? size_t(bytes) : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? size_t(pages) * size_t(page) : 0;
#endif
}

static constexpr size_t kMaxEnvelope = size_t(2560) << 20; // 2.5 GiB: 512 MiB of decoded frames, 2 GiB of uploads
static constexpr size_t kMinEnvelope = size_t(256) << 20;
static constexpr size_t kPooledLimit = size_t(64) << 20;

static size_t defaultEnvelope()
{
    const int mb = qEnvironmentVariableIntValue("NYXMAP_VIDEO_MEMORY_MB");
    if (mb > 0) return size_t(mb) << 20;
    const size_t ram = physicalMemory();
    return ram ? std::clamp(ram / 4, kMinEnvelope, kMaxEnvelope) : kMaxEnvelope;
}

static std::atomic<size_t> s_envelope{defaultEnvelope()};
static std::atomic<size_t> s_decodedOverride{0};
static std::atomic<size_t> s_decoded{0}, s_pooled{0}, s_upload{0};
static std::atomic<int> s_uploadStreams{0};

size_t memoryLimit() { return s_envelope.load(std::memory_order_relaxed); }
void setMemoryLimit(size_t bytes) { s_envelope = bytes ? bytes : defaultEnvelope(); }
size_t memoryInUse() { return s_decoded.load(std::memory_order_relaxed) + s_upload.load(std::memory_order_relaxed); }

// Decoded frames ------------------------------------------------------------
void addDecoded(size_t bytes)
{
    if (bytes) s_decoded.fetch_add(bytes, std::memory_order_relaxed);
}
void removeDecoded(size_t bytes)
{
    if (bytes) s_decoded.fetch_sub(bytes, std::memory_order_relaxed);
}
size_t decodedInUse() { return s_decoded.load(std::memory_order_relaxed); }
size_t decodedLimit()
{
    const size_t o = s_decodedOverride.load(std::memory_order_relaxed);
    return o ? o : memoryLimit() / 5;
}
bool decodedOver() { return decodedInUse() > decodedLimit(); }
void setDecodedLimit(size_t bytes) { s_decodedOverride = bytes; }

static bool reserveUnder(std::atomic<size_t> &used, size_t bytes, size_t limit)
{
    size_t now = used.load(std::memory_order_relaxed);
    for (;;) {
        if (bytes > limit || now > limit - bytes) return false;
        if (used.compare_exchange_weak(now, now + bytes, std::memory_order_relaxed)) return true;
    }
}

bool reservePooled(size_t bytes) { return reserveUnder(s_pooled, bytes, kPooledLimit); }
void releasePooled(size_t bytes)
{
    if (bytes) s_pooled.fetch_sub(bytes, std::memory_order_relaxed);
}

// Upload buffers ------------------------------------------------------------
size_t uploadLimit() { return memoryLimit() - memoryLimit() / 5; }
bool reserveUpload(size_t bytes) { return bytes && reserveUnder(s_upload, bytes, uploadLimit()); }
void releaseUpload(size_t bytes)
{
    if (bytes) s_upload.fetch_sub(bytes, std::memory_order_relaxed);
}
size_t uploadInUse() { return s_upload.load(std::memory_order_relaxed); }
void addUploadStream() { ++s_uploadStreams; }
void removeUploadStream() { --s_uploadStreams; }
int uploadStreams() { return s_uploadStreams.load(std::memory_order_relaxed); }
size_t uploadShare() { return uploadLimit() / size_t(std::max(1, uploadStreams())); }

// Graphics memory ------------------------------------------------------------
static std::atomic<size_t> s_gpu[int(Gpu::Count)];

void addGpu(Gpu kind, size_t bytes)
{
    if (bytes) s_gpu[int(kind)].fetch_add(bytes, std::memory_order_relaxed);
}
void removeGpu(Gpu kind, size_t bytes)
{
    if (bytes) s_gpu[int(kind)].fetch_sub(bytes, std::memory_order_relaxed);
}
size_t gpuInUse(Gpu kind) { return s_gpu[int(kind)].load(std::memory_order_relaxed); }
size_t gpuInUse()
{
    size_t sum = 0;
    for (const auto &g : s_gpu) sum += g.load(std::memory_order_relaxed);
    return sum;
}

// Threads -------------------------------------------------------------------
static std::mutex s_threadMutex;
static int s_threadsInUse = 0, s_decodersOpen = 0;
static std::atomic<int> s_threadOverride{0};

int threadLimit()
{
    if (const int o = s_threadOverride.load(std::memory_order_relaxed); o > 0) return o;
    static const int env = qEnvironmentVariableIntValue("NYXMAP_DECODE_THREAD_BUDGET");
    if (env > 0) return env;
    return 2 * int(std::max(1u, std::thread::hardware_concurrency()));
}
void setThreadLimit(int threads) { s_threadOverride = std::max(0, threads); }

int reserveThreads(int alone)
{
    alone = std::max(1, alone);
    const int limit = threadLimit();
    std::lock_guard<std::mutex> lk(s_threadMutex);
    const int n = s_threadsInUse + alone <= limit ? alone : std::min(alone, std::max(2, limit / (s_decodersOpen + 1)));
    s_threadsInUse += n;
    ++s_decodersOpen;
    return n;
}
void releaseThreads(int threads)
{
    if (threads <= 0) return;
    std::lock_guard<std::mutex> lk(s_threadMutex);
    s_threadsInUse -= threads;
    --s_decodersOpen;
}
int threadsInUse()
{
    std::lock_guard<std::mutex> lk(s_threadMutex);
    return s_threadsInUse;
}
int decodersOpen()
{
    std::lock_guard<std::mutex> lk(s_threadMutex);
    return s_decodersOpen;
}

}
