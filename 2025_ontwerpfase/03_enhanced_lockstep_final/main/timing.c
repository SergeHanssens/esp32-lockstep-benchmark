
// timing.c
// Eenvoudige statistiek en deadline monitoring

#include <stdio.h>
#include "timing.h"

static uint32_t deadline_misses = 0;

void init_timing_analysis(void) {
    deadline_misses = 0;
}

void update_timing(uint64_t duration, bool deadline_miss) {
    if (deadline_miss) deadline_misses++;
}

uint32_t get_miss_count(void) {
    return deadline_misses;
}
