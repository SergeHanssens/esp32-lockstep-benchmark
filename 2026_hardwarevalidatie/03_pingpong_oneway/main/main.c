/*
 * 03_pingpong_oneway v2: rondreis ONTLEED in rondreis / verwerking / residu.
 *
 * Elke core heeft een eigen cycle-counter (CCOUNT) en die lopen niet
 * synchroon -> tijdstempels van core 0 en core 1 zijn NIET direct
 * vergelijkbaar. Daarom meet elke core uitsluitend met zijn eigen teller:
 *   rondreis (initiator)   = heen + verwerking(responder) + terug
 *   verwerking (responder) meet de responder zelf
 *   => residu = rondreis - verwerking = heen + terug SAMEN
 *
 * v2-CORRECTIE (15 aug 2026): deze opzet kan de twee richtingen NIET
 * afzonderlijk meten. Fase 1 (core 0 initieert) en fase 2 (core 1
 * initieert) meten allebei dezelfde som heen+terug; het omkeren van de
 * initiator verwisselt de ROLLEN van de cores, niet de gemeten richting.
 * residu/2 is dus een AFGELEIDE gemiddelde transportbijdrage per traject
 * onder de aanname heen ~= terug, geen gemeten one-way-latentie, en het
 * verschil tussen de fasen is een ROLVERSCHIL, geen symmetriebewijs.
 * (Voor echte per-richting-latenties is een gemeenschappelijke tijdbasis
 * of externe observatie nodig.) v1 noemde dit ten onrechte een gemeten
 * symmetrie. v2 voegt ook WARMUP-rondes toe die niet worden meegeteld.
 *
 * FreeRTOS enkel voor het opstarten van 1 taak per core; meetpad is
 * volatile gedeeld geheugen + spin-wait + memw (zoals 02_pingpong).
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#define RONDES 100
#define WARMUP 10
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
    for (int i = 0; i < WARMUP + RONDES; i++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        k.data_ab = (uint32_t)i;
        __asm__ __volatile__("memw");
        k.vlag_ab = 1;
        while (k.vlag_ba == 0) { }
        uint32_t t3 = esp_cpu_get_cycle_count();
        k.vlag_ba = 0;
        (void)k.data_ba;
        if (i >= WARMUP) rt[i - WARMUP] = t3 - t0;
    }
}

static void speel_responder(uint32_t *verw)
{
    for (int i = 0; i < WARMUP + RONDES; i++) {
        while (k.vlag_ab == 0) { }
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t d = k.data_ab;
        k.vlag_ab = 0;
        k.data_ba = d + 1;
        __asm__ __volatile__("memw");
        uint32_t t2 = esp_cpu_get_cycle_count();
        k.vlag_ba = 1;
        if (i >= WARMUP) verw[i - WARMUP] = t2 - t1;
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
    printf("\n=== RESULTATEN v2 (%d meetrondes per fase na %d warm-uprondes, 15 aug 2026) ===\n",
           RONDES, WARMUP);
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
    printf("--- Residu = rondreis - verwerking (heen + terug SAMEN) ---\n");
    stats("residu fase 1 (c0 init)", tr1, RONDES);
    stats("residu fase 2 (c1 init)", tr2, RONDES);
    uint64_t s1 = 0, s2 = 0;
    for (int i = 0; i < RONDES; i++) { s1 += tr1[i]; s2 += tr2[i]; }
    double g1 = (double)s1 / RONDES, g2 = (double)s2 / RONDES;
    printf("\nRolverschil initiator c0 vs c1 (GEEN richtingssymmetrie-bewijs): %.1f cycli (%.1f%%)\n",
           g1 > g2 ? g1 - g2 : g2 - g1, 100.0 * ((g1 > g2 ? g1 - g2 : g2 - g1) / ((g1 + g2) / 2)));
    printf("=> afgeleide gemiddelde transportbijdrage per traject (residu/2,\n");
    printf("   onder de aanname heen ~= terug) ~= %.1f cycli ~= %.3f us\n",
           (g1 + g2) / 4.0, ((g1 + g2) / 4.0) / CPU_MHZ);
    printf("BEWIJS: v2 15 aug 2026; rondreis is de primaire grootheid; per-core eigen CCOUNT;\n");
    printf("%d warm-uprondes per fase uitgesloten van de statistiek.\n", WARMUP);
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
    printf("=== 03_pingpong_oneway v2: rondreis-ontleding core 0 <-> core 1 ===\n");
    xTaskCreatePinnedToCore(taak_core1, "c1", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_core0, "c0", 16384, NULL, 5, NULL, 0);
}
