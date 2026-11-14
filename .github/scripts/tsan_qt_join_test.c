#define _GNU_SOURCE
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
static void *worker(void *value) { return value; }
static void *slow(void *value) {
    struct timespec duration = {0, 50000000};
    nanosleep(&duration, 0);
    return value;
}
int main(void) {
    for (int i = 0; i < 128; ++i) {
        pthread_t thread; void *result = 0;
        if (pthread_create(&thread, 0, worker, (void *)(intptr_t)42)) return 1;
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline); deadline.tv_sec += 2;
        if (pthread_clockjoin_np(thread, &result, CLOCK_MONOTONIC, i % 2 ? &deadline : 0) || (intptr_t)result != 42) return 2;
    }
    pthread_t thread;
    if (pthread_create(&thread, 0, slow, 0)) return 3;
    struct timespec past = {0, 0};
    if (pthread_clockjoin_np(thread, 0, CLOCK_MONOTONIC, &past) != ETIMEDOUT) return 4;
    if (pthread_clockjoin_np(thread, 0, CLOCK_REALTIME, 0)) return 5;
    puts("Qt clock join bridge: joins, timeout and thread-ID reuse passed");
    return 0;
}
