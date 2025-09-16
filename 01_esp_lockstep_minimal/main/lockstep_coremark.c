
// lockstep_coremark.c
// Twee taken: één draait op Core 0, de ander op Core 1
// Beide voeren een benchmark uit en synchroniseren via een Queue

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lockstep_coremark.h"
#include "lockstep_sync.h"
#include "coremark_stub.h"
#include "esp_timer.h"

#define MAX_SAMPLE_DURATION_US 10000  // 10 milliseconden deadline

static uint32_t laatste_resultaat_core0 = 0;

void task_core0_main(void *pvParameters) {
    while (1) {
        uint64_t start = esp_timer_get_time();
        uint32_t result = run_coremark_stub(10000);
        uint64_t duration = esp_timer_get_time() - start;

        sync_with_queue_send(result);
        laatste_resultaat_core0 = result;

        printf("[Core0] Resultaat: %lu, Tijd: %llu us", result, duration);
        if (duration > MAX_SAMPLE_DURATION_US) {
            printf(" ⚠️ Deadline overschreden!");
        }
        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_core1_checker(void *pvParameters) {
    while (1) {
        uint32_t ref_result = sync_with_queue_receive();

        uint64_t start = esp_timer_get_time();
        uint32_t my_result = run_coremark_stub(10000);
        uint64_t duration = esp_timer_get_time() - start;

        int verschil = (int)my_result - (int)ref_result;
        printf("[Core1] Checker: %lu (verschil: %d), Tijd: %llu us", my_result, verschil, duration);
        if (duration > MAX_SAMPLE_DURATION_US) {
            printf(" ⚠️ Deadline overschreden!");
        }
        if (abs(verschil) > 10) {
            printf(" ❌ Mismatch gedetecteerd!");
        } else {
            printf(" ✅ OK");
        }
        printf("\n");
    }
}
