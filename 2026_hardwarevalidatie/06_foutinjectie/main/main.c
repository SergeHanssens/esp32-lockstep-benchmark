/*
 * 06_foutinjectie: foutinjectie op de lockstep-kern van 05 (zelfde
 * architectuur: app-core 0 rekent, checker-core 1 controleert invoer/
 * verwerking/uitvoer via gedeeld geheugen, data -> memw -> vlag).
 *
 * Twee campagnes in een run:
 *
 * CAMPAGNE A - gerichte injecties (deterministisch, reproduceerbaar):
 *   in 1 op de 3 rondes flipt de app-core precies 1 bit op een gekozen
 *   punt in de pijplijn: in het gedeelde invoerblok (na schrijven, voor
 *   vlag), in het rekenresultaat (na berekening) of in de uitvoer (alleen
 *   de weggeschreven waarde). Random woord/bit via xorshift op de
 *   ronde-index. Per injectie meten we het verdict EN de detectielatentie
 *   (cycli van flip tot ontvangst van het verdict). Verwachting:
 *     invoerflip  -> verdict INVOER|VERWERKING|VERWERKING-gevolg = 0x7
 *                    (corrupte invoer propageert door alles)
 *     resultaatflip -> 0x6 (VERWERKING|UITVOER, zoals de zelftest in 05)
 *     uitvoerflip -> 0x4 (alleen UITVOER)
 *   en 100% detectie: elke injectie valt binnen het detectievenster.
 *
 * CAMPAGNE B - asynchrone injecties (realistisch SEU-scenario):
 *   een esp_timer flipt elke 250 us een willekeurige bit in het gedeelde
 *   kanaal (32 invoerwoorden + resultaat + uitvoer) terwijl de lockstep
 *   50.000 rondes draait. Hier is detectie NIET gegarandeerd: een flip
 *   buiten het levende venster van de data (al gelezen, of wordt zo
 *   overschreven) is gemaskeerd - precies zoals bij echte SEU's. We
 *   rapporteren injecties, detecties per categorie en de maskeringsgraad.
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"
#include "esp_timer.h"

#define RONDES_A        999         /* campagne A: injectie als ronde % 3 == 1 */
#define RONDES_B        50000       /* campagne B: async injecties via timer */
#define BLOK            32
#define CPU_MHZ         240.0
#define INJ_PERIODE_US  250         /* timerperiode campagne B */

#define FOUT_INVOER     (1u << 0)
#define FOUT_VERWERKING (1u << 1)
#define FOUT_UITVOER    (1u << 2)

#define DOEL_INVOER     0
#define DOEL_RESULTAAT  1
#define DOEL_UITVOER    2

typedef struct {
    volatile uint32_t invoer[BLOK];
    volatile uint32_t crc_invoer;
    volatile uint32_t res_app;
    volatile uint32_t uitvoer;
    volatile uint32_t vlag_invoer;
    volatile uint32_t vlag_res;
    volatile uint32_t vlag_uit;
    volatile uint32_t vlag_klaar;
    volatile uint32_t verdict;
} kanaal_t;

static kanaal_t k;
static uint32_t app_invoer[BLOK];
static uint32_t chk_invoer[BLOK];

/* administratie campagne A: 333 injecties */
#define MAX_INJ_A 333
static struct {
    uint16_t ronde; uint8_t doel; uint8_t woord; uint8_t bit;
    uint8_t verdict; uint32_t latentie;
} inj_a[MAX_INJ_A];
static uint32_t n_inj_a = 0;

/* administratie campagne B (alleen tellers; geen arrays van 50k) */
static volatile uint32_t n_inj_b = 0;
static uint32_t b_det_invoer = 0, b_det_verwerking = 0, b_det_uitvoer = 0;
static uint32_t b_rondes_met_fout = 0;

/* --- workload: identiek aan 05_lockstep_kern ----------------------------- */

static inline uint32_t xs32(uint32_t x)
{
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

static void genereer_invoer(uint32_t ronde, uint32_t *buf)
{
    uint32_t s = ronde * 2654435761u + 1u;
    for (int i = 0; i < BLOK; i++) { s = xs32(s); buf[i] = s; }
}

static inline uint32_t crc_stap(uint32_t crc, uint32_t w)
{
    return (crc * 33u) ^ w;
}

static uint32_t bewerk(const uint32_t *b)
{
    uint32_t r = 0x12345678u;
    for (int i = 0; i < BLOK; i++) {
        r = (r << 1) | (r >> 31);
        r ^= b[i];
        r += b[i] >> 3;
    }
    return r;
}

/* --- injector campagne B (esp_timer, asynchroon t.o.v. de lockstep-lus) -- */

static void injecteer_cb(void *arg)
{
    static uint32_t s = 0xDEADBEEFu;
    s = xs32(s);
    uint32_t doel = s % (BLOK + 2);          /* 32 invoerwoorden + res + uit */
    uint32_t masker = 1u << (xs32(s ^ 0xA5A5A5A5u) & 31);
    if (doel < BLOK)          k.invoer[doel] ^= masker;
    else if (doel == BLOK)    k.res_app     ^= masker;
    else                      k.uitvoer     ^= masker;
    n_inj_b++;
}

/* --- checkercore (core 1): identiek aan 05, weet niets van injecties ----- */

static void taak_checker(void *arg)
{
    printf("[core %d] checkercore gestart\n", xPortGetCoreID());
    for (uint32_t ronde = 0; ronde < RONDES_A + RONDES_B; ronde++) {
        uint32_t verdict = 0;
        while (k.vlag_invoer == 0) { }
        k.vlag_invoer = 0;
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) {
            chk_invoer[i] = k.invoer[i];
            crc = crc_stap(crc, chk_invoer[i]);
        }
        if (crc != k.crc_invoer) verdict |= FOUT_INVOER;

        uint32_t res_chk = bewerk(chk_invoer);
        while (k.vlag_res == 0) { }
        k.vlag_res = 0;
        if (k.res_app != res_chk) verdict |= FOUT_VERWERKING;

        while (k.vlag_uit == 0) { }
        k.vlag_uit = 0;
        if (k.uitvoer != res_chk) verdict |= FOUT_UITVOER;

        k.verdict = verdict;
        __asm__ __volatile__("memw");
        k.vlag_klaar = 1;
    }
    printf("[core %d] checkercore klaar\n", xPortGetCoreID());
    vTaskDelete(NULL);
}

/* --- applicatiecore (core 0) --------------------------------------------- */

static void taak_app(void *arg)
{
    printf("[core %d] applicatiecore gestart\n", xPortGetCoreID());

    /* ==== CAMPAGNE A: gerichte injecties met latentiemeting ==== */
    for (uint32_t ronde = 0; ronde < RONDES_A; ronde++) {
        int injecteer = (ronde % 3u == 1u);
        uint32_t doel  = (ronde / 3u) % 3u;
        uint32_t woord = xs32(ronde * 11u + 3u) % BLOK;
        uint32_t masker = 1u << (xs32(ronde * 17u + 5u) & 31);
        uint32_t t_flip = 0;

        genereer_invoer(ronde, app_invoer);
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) {
            k.invoer[i] = app_invoer[i];
            crc = crc_stap(crc, app_invoer[i]);
        }
        k.crc_invoer = crc;
        if (injecteer && doel == DOEL_INVOER) {
            /* transportfout: gedeeld blok corrupt, CRC beschrijft de
             * correcte data -> checker moet dit zien bij het inlezen */
            k.invoer[woord] ^= masker;
            t_flip = esp_cpu_get_cycle_count();
        }
        __asm__ __volatile__("memw");
        k.vlag_invoer = 1;

        uint32_t res = bewerk(app_invoer);
        if (injecteer && doel == DOEL_RESULTAAT) {
            res ^= masker;                    /* verwerkingsfout */
            t_flip = esp_cpu_get_cycle_count();
        }
        k.res_app = res;
        __asm__ __volatile__("memw");
        k.vlag_res = 1;

        uint32_t uit = res;
        if (injecteer && doel == DOEL_UITVOER) {
            uit ^= masker;                    /* schrijffout: alleen uitvoer */
            t_flip = esp_cpu_get_cycle_count();
        }
        k.uitvoer = uit;
        __asm__ __volatile__("memw");
        k.vlag_uit = 1;

        while (k.vlag_klaar == 0) { }
        uint32_t verdict = k.verdict;
        k.vlag_klaar = 0;
        uint32_t t_verdict = esp_cpu_get_cycle_count();

        if (injecteer && n_inj_a < MAX_INJ_A) {
            inj_a[n_inj_a].ronde   = (uint16_t)ronde;
            inj_a[n_inj_a].doel    = (uint8_t)doel;
            inj_a[n_inj_a].woord   = (uint8_t)woord;
            inj_a[n_inj_a].bit     = (uint8_t)(31 - __builtin_clz(masker));
            inj_a[n_inj_a].verdict = (uint8_t)verdict;
            inj_a[n_inj_a].latentie = t_verdict - t_flip;
            n_inj_a++;
        }
    }
    printf("[campagne A klaar: %u injecties]\n", (unsigned)n_inj_a);

    /* ==== CAMPAGNE B: asynchrone timer-injecties ==== */
    esp_timer_handle_t th;
    const esp_timer_create_args_t targs = {
        .callback = injecteer_cb, .name = "injector"
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, INJ_PERIODE_US));

    for (uint32_t ronde = 0; ronde < RONDES_B; ronde++) {
        genereer_invoer(RONDES_A + ronde, app_invoer);
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) {
            k.invoer[i] = app_invoer[i];
            crc = crc_stap(crc, app_invoer[i]);
        }
        k.crc_invoer = crc;
        __asm__ __volatile__("memw");
        k.vlag_invoer = 1;

        uint32_t res = bewerk(app_invoer);
        k.res_app = res;
        __asm__ __volatile__("memw");
        k.vlag_res = 1;

        k.uitvoer = res;
        __asm__ __volatile__("memw");
        k.vlag_uit = 1;

        while (k.vlag_klaar == 0) { }
        uint32_t verdict = k.verdict;
        k.vlag_klaar = 0;

        if (verdict != 0) {
            b_rondes_met_fout++;
            if (verdict & FOUT_INVOER)     b_det_invoer++;
            if (verdict & FOUT_VERWERKING) b_det_verwerking++;
            if (verdict & FOUT_UITVOER)    b_det_uitvoer++;
        }
    }
    ESP_ERROR_CHECK(esp_timer_stop(th));
    uint32_t inj_b_totaal = n_inj_b;
    printf("[campagne B klaar: %u rondes, %u injecties]\n",
           (unsigned)RONDES_B, (unsigned)inj_b_totaal);

    /* --- rapportage ------------------------------------------------------ */
    static const char *doelnaam[3] = { "invoer", "resultaat", "uitvoer" };
    static const uint8_t verwacht[3] = { 0x7, 0x6, 0x4 };

    printf("\n=== RESULTATEN foutinjectie (8 aug 2026) ===\n");
    printf("--- CAMPAGNE A: %u gerichte injecties (1 bitflip per injectie) ---\n",
           (unsigned)n_inj_a);
    for (int d = 0; d < 3; d++) {
        uint32_t tot = 0, ok = 0, gemist = 0;
        uint32_t lmin = UINT32_MAX, lmax = 0; uint64_t lsom = 0;
        for (uint32_t i = 0; i < n_inj_a; i++) {
            if (inj_a[i].doel != d) continue;
            tot++;
            if (inj_a[i].verdict == verwacht[d]) ok++;
            if (inj_a[i].verdict == 0) gemist++;
            uint32_t l = inj_a[i].latentie;
            lsom += l; if (l < lmin) lmin = l; if (l > lmax) lmax = l;
        }
        printf("doelwit %-9s: %3u injecties, %3u exact verwacht verdict 0x%x, %u NIET gedetecteerd\n",
               doelnaam[d], (unsigned)tot, (unsigned)ok, verwacht[d], (unsigned)gemist);
        printf("   detectielatentie: min %5u / gem %7.1f / max %5u cycli (gem %.3f us)\n",
               (unsigned)lmin, (double)lsom / tot, (unsigned)lmax,
               ((double)lsom / tot) / CPU_MHZ);
    }
    uint32_t a_gedetecteerd = 0;
    for (uint32_t i = 0; i < n_inj_a; i++) if (inj_a[i].verdict != 0) a_gedetecteerd++;
    printf("detectiegraad campagne A: %u/%u = %.1f%%\n",
           (unsigned)a_gedetecteerd, (unsigned)n_inj_a,
           100.0 * a_gedetecteerd / n_inj_a);

    printf("--- CAMPAGNE B: asynchrone injecties (elke %d us, willekeurige bit) ---\n",
           INJ_PERIODE_US);
    printf("rondes %u, injecties %u, rondes met gedetecteerde fout %u\n",
           (unsigned)RONDES_B, (unsigned)inj_b_totaal, (unsigned)b_rondes_met_fout);
    printf("detecties per categorie: invoer %u / verwerking %u / uitvoer %u\n",
           (unsigned)b_det_invoer, (unsigned)b_det_verwerking, (unsigned)b_det_uitvoer);
    printf("gemaskeerd of samengevallen: %d (%.1f%% van de injecties)\n",
           (int)inj_b_totaal - (int)b_rondes_met_fout,
           100.0 * ((int)inj_b_totaal - (int)b_rondes_met_fout) / (double)inj_b_totaal);
    printf("(maskering = flip buiten het levende venster van de data: al door de\n");
    printf(" checker gelezen, of voor het lezen alweer overschreven door een\n");
    printf(" volgende ronde - zoals bij echte SEU's; meerdere injecties kunnen\n");
    printf(" ook in dezelfde ronde samenvallen)\n");

    /* parseerbare regels voor de CSV-analyse (scripts/parse_foutinjectie.py) */
    printf("\nCSV_START\n");
    printf("INJ;ronde;doelwit;woord;bit;verdict;latentie_cycli\n");
    for (uint32_t i = 0; i < n_inj_a; i++) {
        printf("INJ;%u;%s;%u;%u;0x%x;%u\n",
               (unsigned)inj_a[i].ronde, doelnaam[inj_a[i].doel],
               (unsigned)inj_a[i].woord, (unsigned)inj_a[i].bit,
               (unsigned)inj_a[i].verdict, (unsigned)inj_a[i].latentie);
    }
    printf("CSV_EIND\n");
    printf("BEWIJS: alle tellers en latenties komen uit deze run op het bord.\n");
    printf("=== KLAAR ===\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("=== 06_foutinjectie: lockstep-kern + gerichte en asynchrone bitflips ===\n");
    xTaskCreatePinnedToCore(taak_checker, "chk", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_app, "app", 16384, NULL, 5, NULL, 0);
}
