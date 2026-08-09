
#ifndef CSV_LOGGER_H
#define CSV_LOGGER_H

#include <stdint.h>
#include <stdbool.h>

void log_csv_sample(uint32_t seq, uint32_t ref, uint32_t chk, int delta,
                    uint64_t dur, bool miss, bool mismatch);

#endif
