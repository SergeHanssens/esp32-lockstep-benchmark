
// coremark_stub.c
// Dummy workload als placeholder voor echte CoreMark benchmark

#include "coremark_stub.h"

uint32_t run_coremark_stub(uint32_t iterations) {
    volatile uint32_t result = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        result ^= (result << 1) + (i * 17);
    }
    return result ^ (result >> 3);
}
