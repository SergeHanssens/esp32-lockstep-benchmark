/*
 * 03_pingpong_oneway_rtos: zelfde rondreis-ontleding als 03_pingpong_oneway,
 * maar het transport loopt via FreeRTOS-queues i.p.v. volatile gedeeld
 * geheugen. Alleen het transportmechanisme verschilt; meetopzet, aantal
 * rondes, fase-omkering en rapportage zijn identiek, zodat het verschil
 * 1-op-1 vergelijkbaar is.
 *
 * Meetpunten (gedocumenteerd omdat de queue-API de grens bepaalt):
 *   initiator:  t0 vlak voor xQueueSend(heen), t3 vlak na xQueueReceive(terug)
 *   responder:  t1 vlak na xQueueReceive(heen), t2 vlak voor xQueueSend(terug)
 *   verwerking = t2 - t1 (alleen de +1-transformatie, eigen CCOUNT)
 *   transport  = rondreis - verwerking  (bevat dus ALLE queue-overhead:
 *                send, context/scheduler-werk, wakker worden uit receive)
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
#define CPU_MHZ 160.0

static QueueHandle_t q_heen;   /* initiator -> responder */
static QueueHandle_t q_terug;  /* responder -> initiator */
static volatile uint32_t stap; /* fase-barrier (zoals in 03) */

static uint32_t rt_f1[RONDES], verw_f1[RONDES];   /* fase 1: c0 init, c1 resp */
static uint32_t rt_f2[RONDES], verw_f2[RONDES];   /* fase 2: c1 init, c0 resp */

static void speel_initiator(uint32_t *rt)
{
    for (uint32_t i = 0; i < RONDES; i++) {
        uint32_t d;
        uint32_t t0 = esp_cpu_get_cycle_count();
        xQueueSend(q_heen, &i, portMAX_DELAY);
        xQueueReceive(q_terug, &d, portMAX_DELAY);
        uint32_t t3 = esp_cpu_get_cycle_count();
        (void)d;
        rt[i] = t3 - t0;
    }
}

static void speel_responder(uint32_t *verw)
{
    for (int i = 0; i < RONDES; i++) {
        uint32_t d, a;
        xQueueReceive(q_heen, &d, portMAX_DELAY);
        uint32_t t1 = esp_cpu_get_cycle_count();
        a = d + 1;
        uint32_t t2 = esp_cpu_get_cycle_count();
        xQueueSend(q_terug, &a, portMAX_DELAY);
        verw[i] = t2 - t1;
    }
}

static void wacht_op_stap(uint32_t s)
{
    while (stap < s) { vTaskDelay(1); }
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

    printf("\n=== RESULTATEN (%d rondes per fase, 8 aug 2026, transport = FreeRTOS-queues) ===\n", RONDES);
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
    printf("--- Transport = rondreis - verwerking (heen + terug samen, incl. queue-overhead) ---\n");
    stats("transport richting 0->1->0", tr1, RONDES);
    stats("transport richting 1->0->1", tr2, RONDES);
    uint64_t s1 = 0, s2 = 0;
    for (int i = 0; i < RONDES; i++) { s1 += tr1[i]; s2 += tr2[i]; }
    double g1 = (double)s1 / RONDES, g2 = (double)s2 / RONDES;
    printf("\nSymmetrie-check: verschil tussen beide richtingen: %.1f cycli (%.1f%%)\n",
           g1 > g2 ? g1 - g2 : g2 - g1, 100.0 * ((g1 > g2 ? g1 - g2 : g2 - g1) / ((g1 + g2) / 2)));
    printf("=> enkele reis (heen ~= terug) ~= %.1f cycli ~= %.3f us\n",
           (g1 + g2) / 4.0, ((g1 + g2) / 4.0) / CPU_MHZ);
    printf("BEWIJS: zelfde ontleding als 03_pingpong_oneway, transport via xQueueSend/xQueueReceive.\n");
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
    printf("=== 03_pingpong_oneway_rtos: heen/terug-ontleding via FreeRTOS-queues ===\n");
    q_heen  = xQueueCreate(1, sizeof(uint32_t));
    q_terug = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreatePinnedToCore(taak_core1, "c1", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_core0, "c0", 16384, NULL, 5, NULL, 0);
}
