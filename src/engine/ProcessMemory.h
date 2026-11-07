#pragma once
// The memory the process really holds (resident set), whatever allocated it: NyxMap, Qt, FFmpeg, the drivers. The
// internal budgets (decoded frames, upload buffers) count only NyxMap's own; a leak shows here first.
#include <cstdint>

namespace processMemory {
// Bytes resident now; 0 when the system does not tell
uint64_t residentBytes();

struct SystemUsage {
    double cpu = -1, gpu = -1, ram = -1, vram = -1; // percentages; -1 when unavailable
};

// One instance per observer. CPU uses the delta between consecutive samples.
class SystemUsageSampler {
public:
    SystemUsage sample();
private:
    uint64_t previousTotal = 0, previousIdle = 0;
};
}
