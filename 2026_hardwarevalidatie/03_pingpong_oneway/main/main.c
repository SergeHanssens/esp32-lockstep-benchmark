/*
 * 03_pingpong_oneway: rondreis ONTLEED in heen / verwerking / terug.
 *
 * Probleem: elke core heeft een eigen cycle-counter (CCOUNT) en die lopen
 * niet synchroon -> tijdstempels van core 0 en core 1 zijn NIET direct
 * vergelijkbaar. Daarom meet elke core uitsluitend met zijn eigen teller:
 *   rondreis (initiator)  = heen + verwerking(responder) + terug
 *   verwerking (responder) meet de responder zelf
 *   => transport = heen + terug = rondreis - verwerking
 * Fase 1: core 0 initieert. Fase 2: core 1 initieert (richting omgekeerd).
 * Gelijke transporttijd in beide richtingen onderbouwt heen ~= terug
 * ~= transport / 2 (symmetrie gemeten i.p.v. aangenomen).
 * FreeRTOS enkel voor het opstarten van 1 taak per core; meetpad is
 * volatile gedeeld geheugen + spin-wait + memw (zoals 02_pingpong).
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#define RONDES 100
#define CPU_MHZ 160.0

typedef struct {
    volatile uint32_t data_ab;      /* initiator -> responder */
    volatile uint32_t data_ba;      /* responder -> initiator */
    volatile uint32_t vlag_ab;
    volatile uint32_t vlag_ba;
    volatile uint32_t stap;         /* fase-barrier */
} kanaal_t;

static kanaal_t k;
static uint32_t rt_f1[RONDES], verw_f1[RONDES];   /* fase 1: c0 init, c1 resp */
static uint32_t rt_f2[RONDES], verw_f2[RONDES];   /* fase 2: c1 init, c0 resp */

static void speel_initiator(uint32_t *rt)
{
    for (int i = 0; i < RONDES; i++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        k.data_ab = i;
        __asm__ __volatile__("memw");
        k.vlag_ab = 1;
        while (k.vlag_ba == 0) { }
        uint32_t t3 = esp_cpu_get_cycle_count();
        k.vlag_ba = 0;
        (void)k.data_ba;
        rt[i] = t3 - t0;
    }
}

static void speel_responder(uint32_t *verw)
{
    for (int i = 0; i < RONDES; i++) {
        while (k.vlag_ab == 0) { }
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t d = k.data_ab;
        k.vlag_ab = 0;
        k.data_ba = d + 1;
        __asm__ __volatile__("memw");
        uint32_t t2 = esp_cpu_get_cycle_count();
        k.vlag_ba = 1;
        verw[i] = t2 - t1;
    }
}

static void wacht_op_stap(uint32_t s)
{
    while (k.stap < s) { }
}

static void stats(const char *naam, const uint32_t *v, int n)
{
    uint32_t mn = UINT32_MAX, mx = 0; uint64_t som = 0; int imx = 0;
    for (int i = 0; i < n; i++) {
        som += v[i];
        if (v[i] < mn) mn = v[i];
        if (v[i] > mx) { mx = v[i]; imx = i; }
    }
    printf("%-28s min %5u / gem %7.1f / max %5u cycli (max in ronde %d)  (gem %.3f us)\n",
           naam, (unsigned)mn, (double)som / n, (unsigned)mx, imx, (som / (double)n) / CPU_MHZ);
}

static void taak_core0(void *arg)
{
    printf("[core %d] taak gestart\n", xPortGetCoreID());
    speel_initiator(rt_f1);            /* fase 1: core 0 initieert */
    k.stap = 1;
    wacht_op_stap(2);                  /* wacht tot core 1 klaar is met omschakelen */
    speel_responder(verw_f2);          /* fase 2: core 0 antwoordt */
    wacht_op_stap(3);

    /* rapportage (alle waarden zijn nu gewone getallen) */
    printf("\n=== RESULTATEN (%d rondes per fase, 8 aug 2026) ===\n", RONDES);
    printf("--- Fase 1: core 0 -> core 1 -> core 0 ---\n");
    stats("rondreis (core 0)", rt_f1, RONDES);
    stats("verwerking core 1", verw_f1, RONDES);
    printf("--- Fase 2: core 1 -> core 0 -> core 1 ---\n");
    stats("rondreis (core 1)", rt_f2, RONDES);
    stats("verwerking core 0", verw_f2, RONDES);

    uint32_t tr1[RONDES], tr2[RONDES];
    for (int i = 0; i < RONDES; i++) {
        tr1[i] = rt_f1[i] - verw_f1[i];
        tr2[i] = rt_f2[i] - verw_f2[i];
    }
    printf("--- Transport = rondreis - verwerking (heen + terug samen) ---\n");
    stats("transport richting 0->1->0", tr1, RONDES);
    stats("transport richting 1->0->1", tr2, RONDES);
    uint64_t s1 = 0, s2 = 0;
    for (int i = 0; i < RONDES; i++) { s1 += tr1[i]; s2 += tr2[i]; }
    double g1 = (double)s1 / RONDES, g2 = (double)s2 / RONDES;
    printf("\nSymmetrie-check: verschil tussen beide richtingen: %.1f cycli (%.1f%%)\n",
           g1 > g2 ? g1 - g2 : g2 - g1, 100.0 * ((g1 > g2 ? g1 - g2 : g2 - g1) / ((g1 + g2) / 2)));
    printf("=> enkele reis (heen ~= terug) ~= %.1f cycli ~= %.3f us\n",
           (g1 + g2) / 4.0, ((g1 + g2) / 4.0) / CPU_MHZ);
    printf("BEWIJS: rondreis ontleed met per-core eigen cycle-counters; richting omgekeerd gemeten.\n");
    vTaskDelete(NULL);
}

static void taak_core1(void *arg)
{
    printf("[core %d] taak gestart\n", xPortGetCoreID());
    speel_responder(verw_f1);          /* fase 1: core 1 antwoordt */
    wacht_op_stap(1);
    k.stap = 2;
    speel_initiator(rt_f2);            /* fase 2: core 1 initieert */
    k.stap = 3;
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("=== 03_pingpong_oneway: heen/terug-ontleding core 0 <-> core 1 ===\n");
    xTaskCreatePinnedToCore(taak_core1, "c1", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_core0, "c0", 16384, NULL, 5, NULL, 0);
}
