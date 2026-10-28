#pragma once
// The memory the process really holds (resident set), whatever allocated it: NyxMap, Qt, FFmpeg, the drivers. The
// internal budgets (decoded frames, upload buffers) count only NyxMap's own; a leak shows here first.
#include <cstdint>

namespace processMemory {
// Bytes resident now; 0 when the system does not tell
uint64_t residentBytes();
}
