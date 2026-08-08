/*
 * Pingpong-test: core 0 <-> core 1 via gedeeld geheugen.
 * Bewijs dat beide cores apart draaien en berichten uitwisselen.
 * FreeRTOS wordt ENKEL gebruikt om op elke core een taak te starten;
 * in het meetpad zelf: volatile gedeeld geheugen + spin-wait + memory barrier.
 * (Aanpak conform notitie 008 en de werksessie met promotor, dec 2024.)
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#define RONDES 10
#define CPU_MHZ 160.0   /* cpu freq: 160 MHz, zie bootlog */

typedef struct {
    volatile uint32_t naar_core1;   /* bericht core0 -> core1 */
    volatile uint32_t naar_core0;   /* antwoord core1 -> core0 */
    volatile uint32_t vlag_c1;      /* 1 = bericht klaar voor core1 */
    volatile uint32_t vlag_c0;      /* 1 = antwoord klaar voor core0 */
} pingpong_t;

static pingpong_t pp;

static void checker_core1(void *arg)
{
    printf("[core %d] checker-taak gestart op core %d\n",
           xPortGetCoreID(), xPortGetCoreID());
    for (int i = 0; i < RONDES; i++) {
        while (pp.vlag_c1 == 0) { }          /* spin-wait op bericht */
        uint32_t ontvangen = pp.naar_core1;
        pp.vlag_c1 = 0;
        pp.naar_core0 = ontvangen + 1;       /* bewerk en stuur terug */
        __asm__ __volatile__("memw");        /* memory barrier */
        pp.vlag_c0 = 1;
    }
    printf("[core %d] checker-taak klaar na %d rondes\n", xPortGetCoreID(), RONDES);
    vTaskDelete(NULL);
}

static void app_core0(void *arg)
{
    printf("[core %d] app-taak gestart op core %d\n",
           xPortGetCoreID(), xPortGetCoreID());
    uint32_t min_c = UINT32_MAX, max_c = 0;
    uint64_t som = 0;
    for (int i = 0; i < RONDES; i++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        pp.naar_core1 = 1000 + i;
        __asm__ __volatile__("memw");
        pp.vlag_c1 = 1;
        while (pp.vlag_c0 == 0) { }          /* wacht op antwoord */
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t antwoord = pp.naar_core0;
        pp.vlag_c0 = 0;
        uint32_t d = t1 - t0;
        som += d;
        if (d < min_c) min_c = d;
        if (d > max_c) max_c = d;
        printf("[core %d] ronde %2d: stuurde %4d, ontving %4d terug van core 1, rondreis %5u cycli (%.2f us)\n",
               xPortGetCoreID(), i, 1000 + i, (int)antwoord, (unsigned)d, d / CPU_MHZ);
    }
    printf("[core %d] RESULTAAT: rondreis min %u / gem %.0f / max %u cycli @ %.0f MHz\n",
           xPortGetCoreID(), (unsigned)min_c, (double)som / RONDES, (unsigned)max_c, CPU_MHZ);
    printf("BEWIJS: core 0 en core 1 draaiden elk hun eigen taak en wisselden %d berichten via gedeeld geheugen.\n",
           RONDES);
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("=== Pingpong core 0 <-> core 1 via gedeeld geheugen (8 aug 2026) ===\n");
    xTaskCreatePinnedToCore(checker_core1, "checker", 4096, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(app_core0, "app", 4096, NULL, 5, NULL, 0);
}
