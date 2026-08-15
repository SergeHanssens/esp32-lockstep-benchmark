/*
 * 03_pingpong_oneway_rtos v2: zelfde rondreis-ontleding als 03_pingpong_oneway,
 * maar het transport loopt via FreeRTOS-queues i.p.v. volatile gedeeld
 * geheugen. Alleen het transportmechanisme verschilt; meetopzet, aantal
 * rondes, fase-omkering en rapportage zijn identiek, zodat de RONDREIS
 * 1-op-1 vergelijkbaar is.
 *
 * Meetpunten (gedocumenteerd omdat de queue-API de grens bepaalt):
 *   initiator:  t0 vlak voor xQueueSend(heen), t3 vlak na xQueueReceive(terug)
 *   responder:  t1 vlak na xQueueReceive(heen), t2 vlak voor xQueueSend(terug)
 *   verwerking = t2 - t1 (alleen de +1-transformatie, eigen CCOUNT)
 *   residu     = rondreis - verwerking  (bevat dus ALLE queue-overhead:
 *                send, context/scheduler-werk, wakker worden uit receive)
 * LET OP: het verwerkingsvenster is hier smaller dan in de shared-variant
 * (daar omvat het ook lezen/vlag wissen/schrijven/memw). De RONDREIS is
 * daarom de primaire vergelijkingsgrootheid tussen beide varianten; de
 * residuen zijn niet exact gelijk afgebakend.
 *
 * v2-CORRECTIES (15 aug 2026):
 * 1. De faseovergang gebruikte vTaskDelay(1): core 0 sliep tot een volgende
 *    RTOS-tick terwijl core 1 al aan fase 2 begon, waardoor de eerste
 *    fase-2-meting die wachttijd bevatte (~803k cycli in de run van
 *    8 aug 2026). Dat was een barriere-artefact van de meetcode, geen
 *    cache-effect. v2 gebruikt dezelfde busy-wait-barrier als de
 *    shared-variant (taken zijn op verschillende cores gepind, dus kort
 *    busy-waiten is veilig).
 * 2. Zoals in de shared-variant: de opzet meet GEEN afzonderlijke
 *    richtingen; residu/2 is een afgeleide onder de aanname heen ~= terug,
 *    en het faseverschil is een rolverschil.
 * 3. WARMUP-rondes toegevoegd; die worden niet meegeteld.
 * Elke core meet uitsluitend met zijn eigen cycle-counter (CCOUNT's van
 * beide cores lopen niet synchroon).
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_cpu.h"

#define RONDES 100
#define WARMUP 10
#define CPU_MHZ 160.0

static QueueHandle_t q_heen;   /* initiator -> responder */
static QueueHandle_t q_terug;  /* responder -> initiator */
static volatile uint32_t stap; /* fase-barrier (zoals in 03) */

static uint32_t rt_f1[RONDES], verw_f1[RONDES];   /* fase 1: c0 init, c1 resp */
static uint32_t rt_f2[RONDES], verw_f2[RONDES];   /* fase 2: c1 init, c0 resp */

static void speel_initiator(uint32_t *rt)
{
    for (uint32_t i = 0; i < WARMUP + RONDES; i++) {
        uint32_t d;
        uint32_t t0 = esp_cpu_get_cycle_count();
        xQueueSend(q_heen, &i, portMAX_DELAY);
        xQueueReceive(q_terug, &d, portMAX_DELAY);
        uint32_t t3 = esp_cpu_get_cycle_count();
        (void)d;
        if (i >= WARMUP) rt[i - WARMUP] = t3 - t0;
    }
}

static void speel_responder(uint32_t *verw)
{
    for (int i = 0; i < WARMUP + RONDES; i++) {
        uint32_t d, a;
        xQueueReceive(q_heen, &d, portMAX_DELAY);
        uint32_t t1 = esp_cpu_get_cycle_count();
        a = d + 1;
        uint32_t t2 = esp_cpu_get_cycle_count();
        xQueueSend(q_terug, &a, portMAX_DELAY);
        if (i >= WARMUP) verw[i - WARMUP] = t2 - t1;
    }
}

static void wacht_op_stap(uint32_t s)
{
    /* v2: busy-wait i.p.v. vTaskDelay(1); zie kopcommentaar, correctie 1. */
    while (stap < s) { }
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
    stap = 1;
    wacht_op_stap(2);
    speel_responder(verw_f2);          /* fase 2: core 0 antwoordt */
    wacht_op_stap(3);

    printf("\n=== RESULTATEN v2 (%d meetrondes per fase na %d warm-uprondes, 15 aug 2026, transport = FreeRTOS-queues) ===\n",
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
    printf("--- Residu = rondreis - verwerking (heen + terug SAMEN, incl. alle queue-overhead) ---\n");
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
    printf("BEWIJS: v2 15 aug 2026; rondreis is de primaire grootheid; busy-wait-barrier;\n");
    printf("%d warm-uprondes per fase uitgesloten van de statistiek.\n", WARMUP);
    vTaskDelete(NULL);
}

static void taak_core1(void *arg)
{
    printf("[core %d] taak gestart\n", xPortGetCoreID());
    speel_responder(verw_f1);          /* fase 1: core 1 antwoordt */
    wacht_op_stap(1);
    stap = 2;
    speel_initiator(rt_f2);            /* fase 2: core 1 initieert */
    stap = 3;
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("=== 03_pingpong_oneway_rtos v2: rondreis-ontleding via FreeRTOS-queues ===\n");
    q_heen  = xQueueCreate(1, sizeof(uint32_t));
    q_terug = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreatePinnedToCore(taak_core1, "c1", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_core0, "c0", 16384, NULL, 5, NULL, 0);
}
