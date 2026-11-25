#pragma once
// Mutexes ThreadSanitizer sees: pxs's (pxs/AnnotatedMutex.h), under the names NyxMap has always used
#include <pxs/AnnotatedMutex.h>

#ifdef PXS_TSAN
#define NYXMAP_TSAN 1
#endif

template <typename Mutex>
using Annotated = pxs::Annotated<Mutex>;
using pxs::tsanAcquire;
using pxs::tsanRelease;
