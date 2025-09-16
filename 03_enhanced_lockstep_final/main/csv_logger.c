
// csv_logger.c
// Output in thesis-geschikte CSV-notatie

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "timing.h"

void log_csv_sample(uint32_t seq, uint32_t ref, uint32_t chk, int delta,
                    uint64_t dur, bool miss, bool mismatch) {
    printf("CSV,%"PRIu32",%"PRIu32",%"PRIu32",%d,%"PRIu64",%s,%s\n",
           seq, ref, chk, delta, dur,
           miss ? "MISS" : "OK",
           mismatch ? "MISMATCH" : "MATCH");
}
