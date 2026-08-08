/*
 * Pingpong-test VARIANT B: core 0 <-> core 1 via FreeRTOS-QUEUES.
 * Zelfde experiment als 02_pingpong (gedeeld geheugen), maar nu met
 * volledig RTOS-gebruik in het meetpad: xQueueSend/xQueueReceive.
 * Doel: het latentieverschil RTOS-API vs gedeeld geheugen recent en
 * reproduceerbaar aantonen (vgl. notitie 008, sep 2025).
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_cpu.h"

#define RONDES 10
#define CPU_MHZ 160.0

static QueueHandle_t q_naar_c1;
static QueueHandle_t q_naar_c0;

static void checker_core1(void *arg)
{
    printf("[core %d] checker-taak gestart op core %d (queue-variant)\n",
           xPortGetCoreID(), xPortGetCoreID());
    for (int i = 0; i < RONDES; i++) {
        uint32_t ontvangen;
        xQueueReceive(q_naar_c1, &ontvangen, portMAX_DELAY);
        uint32_t antwoord = ontvangen + 1;
        xQueueSend(q_naar_c0, &antwoord, portMAX_DELAY);
    }
    printf("[core %d] checker-taak klaar na %d rondes\n", xPortGetCoreID(), RONDES);
    vTaskDelete(NULL);
}

static void app_core0(void *arg)
{
    printf("[core %d] app-taak gestart op core %d (queue-variant)\n",
           xPortGetCoreID(), xPortGetCoreID());
    uint32_t min_c = UINT32_MAX, max_c = 0;
    uint64_t som = 0;
    for (int i = 0; i < RONDES; i++) {
        uint32_t bericht = 1000 + i;
        uint32_t antwoord = 0;
        uint32_t t0 = esp_cpu_get_cycle_count();
        xQueueSend(q_naar_c1, &bericht, portMAX_DELAY);
        xQueueReceive(q_naar_c0, &antwoord, portMAX_DELAY);
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t d = t1 - t0;
        som += d;
        if (d < min_c) min_c = d;
        if (d > max_c) max_c = d;
        printf("[core %d] ronde %2d: stuurde %4d, ontving %4d terug van core 1, rondreis %6u cycli (%.2f us)\n",
               xPortGetCoreID(), i, (int)bericht, (int)antwoord, (unsigned)d, d / CPU_MHZ);
    }
    printf("[core %d] RESULTAAT (FreeRTOS-queues): rondreis min %u / gem %.0f / max %u cycli @ %.0f MHz\n",
           xPortGetCoreID(), (unsigned)min_c, (double)som / RONDES, (unsigned)max_c, CPU_MHZ);
    printf("VERGELIJK: zelfde experiment via gedeeld geheugen: zie 02_pingpong (pingpong_log_20260808.txt)\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("=== Pingpong core 0 <-> core 1 via FREERTOS-QUEUES (8 aug 2026) ===\n");
    q_naar_c1 = xQueueCreate(1, sizeof(uint32_t));
    q_naar_c0 = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreatePinnedToCore(checker_core1, "checker", 4096, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(app_core0, "app", 4096, NULL, 5, NULL, 0);
}
