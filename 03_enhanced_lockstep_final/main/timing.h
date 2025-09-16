
#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>
#include <stdbool.h>

void init_timing_analysis(void);
void update_timing(uint64_t duration, bool deadline_miss);
uint32_t get_miss_count(void);

#endif
