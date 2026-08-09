
// csv_logger.c
// Output in thesis-geschikte CSV-notatie

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "timing.h"

void log_csv_header(void) {
    printf("CSV,seq,ref_score,chk_score,delta,duration_us,deadline,match\n");
}

void log_csv_sample(uint32_t seq, uint32_t ref, uint32_t chk, int delta,
                    uint64_t dur, bool miss, bool mismatch) {

    const char *miss_str     = miss     ? "MISS" : "OK";
    const char *mismatch_str = mismatch ? "ERR"  : "OK";

    printf("CSV,%"
           PRIu32 ",%"
           PRIu32 ",%"
           PRIu32 ",%d,%"
           PRIu64 ",%s,%s\n",
           seq, ref, chk, delta, dur, miss_str, mismatch_str);
}