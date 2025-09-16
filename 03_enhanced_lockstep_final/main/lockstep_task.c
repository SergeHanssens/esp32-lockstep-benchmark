
// lockstep_task.c
// Taken op Core0 en Core1 met timingmeting, benchmark, synchronisatie en logging

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "lockstep_task.h"
#include "lockstep_sync.h"
#include "coremark_stub.h"
#include "timing.h"
#include "csv_logger.h"

#define TEST_DURATION_SECONDS  120
#define COREMARK_ITERATIONS    10000
#define SYNC_METHOD_USED       SYNC_METHOD_QUEUE
#define SAMPLE_INTERVAL_MS     1000
#define DEADLINE_US            10000

void task_core0_main(void *pv) {
    printf("[Core0] Benchmarktaak gestart (looptijd: %ds)\n", TEST_DURATION_SECONDS);
    init_sync_method(SYNC_METHOD_USED);
    init_timing_analysis();

    uint64_t start = esp_timer_get_time();
    uint32_t sequence = 0;

    while ((esp_timer_get_time() - start) < TEST_DURATION_SECONDS * 1000000ULL) {
        uint64_t t0 = esp_timer_get_time();
        uint32_t score = run_coremark_stub(COREMARK_ITERATIONS);
        uint64_t duration = esp_timer_get_time() - t0;

        sync_send(score);
        bool deadline_miss = (duration > DEADLINE_US);
        update_timing(duration, deadline_miss);
        log_csv_sample(sequence, score, score, 0, duration, deadline_miss, false);

        sequence++;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    printf("[Core0] Benchmark klaar.\n");
    vTaskDelete(NULL);
}

void task_core1_checker(void *pv) {
    printf("[Core1] Checker gestart\n");

    uint32_t sequence = 0;
    while (1) {
        uint32_t ref_score = sync_receive();

        uint64_t t0 = esp_timer_get_time();
        uint32_t my_score = run_coremark_stub(COREMARK_ITERATIONS);
        uint64_t duration = esp_timer_get_time() - t0;

        int delta = abs((int)my_score - (int)ref_score);
        bool mismatch = (delta > 10);
        bool deadline_miss = (duration > DEADLINE_US);
        update_timing(duration, deadline_miss);
        log_csv_sample(sequence, ref_score, my_score, delta, duration, deadline_miss, mismatch);

        sequence++;
    }
}
