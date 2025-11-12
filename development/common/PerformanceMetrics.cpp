#include "PerformanceMetrics.h"

// ----------------------------------------------------

#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>

uint64_t getNanos(void) {
    struct timespec ts;
    // RAW avoids NTP slewing and freq adjustments
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ----------------------------------------------------
