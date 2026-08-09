/* Wrapper 07_coremark_lockstep: CoreMark in softwarematige checkpoint-lockstep.
 * Vijf fasen in één firmware, alle op dezelfde geheugenmethode (STACK) zodat
 * de vergelijkingen binnen deze firmware zuiver zijn:
 *   A onbeschermd_vol     : 1 context, 1x20000 iter  (in-situ referentie;
 *                           kruischeck met 04-baseline pass A, RTOS actief)
 *   B beschermd_duaal_vol : 2 contexts, 1x20000 iter (duale uitvoering,
 *                           1 checkpoint op het einde) -> isoleert duale-
 *                           uitvoerings-/buscontentiekost
 *   C onbeschermd_segment : 1 context, 400x50 iter   -> isoleert segmentatiekost
 *   D beschermd_segment   : 2 contexts, 400x50 iter  (HOOFDMETING: checkpoint-
 *                           lockstep, 400 checkpoints per run)
 *   E zelftest_segment    : 2 contexts, 10x50 iter, bewuste bitflip op de
 *                           gepubliceerde crcfinal in segment 5 -> bewijst dat
 *                           het checkpoint fouten detecteert
 * Alle fasen draaien met RTOS actief (vgl. 04 pass A = 584,75 it/s).
 * Iteraties per aanroep lopen via seed4_volatile (officiële runtime-parameter);
 * core_*.c blijft byte-identiek aan upstream.
 * Masterproef Serge Hanssens — 8 aug 2026.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_clk_tree.h"
#include "soc/soc_caps.h"
#include "coremark.h"
#include "core_portme.h"

int coremark_main(void);

typedef struct {
    const char *naam;
    int contexts;        /* 1 = onbeschermd, 2 = lockstep */
    int segmenten;       /* aantal coremark_main-aanroepen */
    int seg_iters;       /* iteraties per segment (via seed4_volatile) */
    int zelftest_seg;    /* segment met bewuste bitflip, -1 = geen */
} fase_t;

static const fase_t FASEN[] = {
    { "onbeschermd_vol",     1,   1, 20000, -1 },
    { "beschermd_duaal_vol", 2,   1, 20000, -1 },
    { "onbeschermd_segment", 1, 400,    50, -1 },
    { "beschermd_segment",   2, 400,    50, -1 },
    { "zelftest_segment",    2,  10,    50,  5 },
};
#define N_FASEN (sizeof FASEN / sizeof FASEN[0])

static void draai_fase(const fase_t *f)
{
    printf("\n--- FASE %s: %d context(en), %d segment(en) x %d iteraties ---\n",
           f->naam, f->contexts, f->segmenten, f->seg_iters);
    ls_reset_log();
    ls_selftest_segment = f->zelftest_seg;
    default_num_contexts = (ee_u32)f->contexts;
    ls_quiet = (f->segmenten > 1);   /* per-segment-rapporten onderdrukken */
    if (f->contexts == 2)
        ls_fase_start();

    int64_t wall0 = esp_timer_get_time();
    for (int s = 0; s < f->segmenten; s++) {
        seed4_volatile = (ee_s32)f->seg_iters;
        coremark_main();
    }
    int64_t wall1 = esp_timer_get_time();

    if (f->contexts == 2)
        ls_fase_stop();
    ls_quiet = 0;

    /* samenvatting uit het per-segment-logboek */
    uint64_t som_ticks = 0;
    uint32_t mismatches = 0;
    uint64_t som_lat = 0; uint32_t max_lat = 0;
    uint32_t n_seg = ls_segment_index < LS_MAX_SEG ? ls_segment_index : LS_MAX_SEG;
    for (uint32_t s = 0; s < n_seg; s++) {
        som_ticks += ls_log_ticks_us[s];
        if (ls_log_verdict[s] != 0) mismatches++;
        som_lat += ls_log_latentie_cyc[s];
        if (ls_log_latentie_cyc[s] > max_lat) max_lat = ls_log_latentie_cyc[s];
    }
    uint64_t iters = (uint64_t)f->segmenten * (uint64_t)f->seg_iters;
    uint64_t wall = (uint64_t)(wall1 - wall0);
    double score_getimed = som_ticks ? (double)iters * 1e6 / (double)som_ticks : 0.0;
    double score_eff     = wall      ? (double)iters * 1e6 / (double)wall      : 0.0;

    /* zelftest-oordeel naar 05-voorbeeld: precies 1 mismatch, in het
     * zelftestsegment, met verdict CRCFINAL */
    const char *zt = "nvt";
    if (f->zelftest_seg >= 0) {
        int ok = (mismatches == 1 &&
                  (uint32_t)f->zelftest_seg < n_seg &&
                  ls_log_verdict[f->zelftest_seg] == LS_FOUT_CRCFINAL);
        zt = ok ? "GESLAAGD" : "GEFAALD";
    }

    printf("iteraties %llu | som getimede segmenten %llu us | wandkloktijd %llu us\n",
           (unsigned long long)iters, (unsigned long long)som_ticks,
           (unsigned long long)wall);
    printf("score getimed %.2f it/s | score effectief (incl. herinit) %.2f it/s\n",
           score_getimed, score_eff);
    if (f->contexts == 2)
        printf("checkpoints %u | mismatches %u | latentie gem %.0f / max %u cycli\n",
               (unsigned)n_seg, (unsigned)mismatches,
               n_seg ? (double)som_lat / n_seg : 0.0, (unsigned)max_lat);
    if (ls_error_count)
        printf("!! %u ERROR-regel(s) in CoreMark-output, eerste: %s\n",
               (unsigned)ls_error_count, ls_eerste_error);
    if (f->zelftest_seg >= 0)
        printf("zelftest checkpoint-detectie (bitflip segment %d): %s\n",
               f->zelftest_seg, zt);

    /* machine-parseerbare regels voor meet_lockstep_runs.py */
    printf("FASE07;%s;%d;%d;%d;%llu;%llu;%llu;%.4f;%.4f;%u;%u;%s\n",
           f->naam, f->contexts, f->segmenten, f->seg_iters,
           (unsigned long long)iters, (unsigned long long)som_ticks,
           (unsigned long long)wall, score_getimed, score_eff,
           (unsigned)ls_error_count, (unsigned)mismatches, zt);
    if (f->segmenten > 1) {
        for (uint32_t s = 0; s < n_seg; s++)
            printf("CSV07;%s;%u;%u;%u;%u\n", f->naam, (unsigned)s,
                   (unsigned)ls_log_ticks_us[s],
                   (unsigned)ls_log_latentie_cyc[s],
                   (unsigned)ls_log_verdict[s]);
    }
}

static void lockstep_taak(void *arg)
{
    (void)arg;
    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,
                                 ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &freq_hz);
    printf("\n=== 07_coremark_lockstep: EEMBC CoreMark in checkpoint-lockstep, "
           "CPU %lu MHz, app-core 0 / checker-core 1 ===\n",
           (unsigned long)(freq_hz / 1000000));
    ls_worker_start();
    vTaskDelay(pdMS_TO_TICKS(100));
    for (unsigned i = 0; i < N_FASEN; i++)
        draai_fase(&FASEN[i]);
    printf("\n=== KLAAR: alle fasen uitgevoerd. Log dit bestand als bewijs. ===\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(lockstep_taak, "ls_app", 32768, NULL, 5, NULL, 0);
}
