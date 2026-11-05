// Test-only bridge: newer Qt calls pthread_clockjoin_np, which TSan does not
// intercept. Route that join through its supported pthread join interceptors.
// Never linked into or packaged with NyxMap.
#define _GNU_SOURCE
#include <pthread.h>
#include <time.h>
#include <errno.h>

int pthread_clockjoin_np(pthread_t thread, void **result, clockid_t clock,
                         const struct timespec *deadline)
{
    if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) return EINVAL;
    if (!deadline) return pthread_join(thread, result);
    if (deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000L) return EINVAL;
    if (clock == CLOCK_REALTIME) return pthread_timedjoin_np(thread, result, deadline);
    struct timespec monotonic, realtime, adjusted;
    if (clock_gettime(CLOCK_MONOTONIC, &monotonic) || clock_gettime(CLOCK_REALTIME, &realtime))
        return errno;
    adjusted.tv_sec = realtime.tv_sec + deadline->tv_sec - monotonic.tv_sec;
    adjusted.tv_nsec = realtime.tv_nsec + deadline->tv_nsec - monotonic.tv_nsec;
    if (adjusted.tv_nsec < 0) { adjusted.tv_nsec += 1000000000L; --adjusted.tv_sec; }
    if (adjusted.tv_nsec >= 1000000000L) { adjusted.tv_nsec -= 1000000000L; ++adjusted.tv_sec; }
    if (adjusted.tv_sec < 0) return ETIMEDOUT;
    return pthread_timedjoin_np(thread, result, &adjusted);
}
