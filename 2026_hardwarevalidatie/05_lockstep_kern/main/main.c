/*
 * 05_lockstep_kern: minimale software-lockstep op de ESP32-S3 (checkpoint 2).
 *
 * Rolverdeling volgens het thesisontwerp:
 *   core 0 = applicatiecore: "leest" invoer in, verwerkt ze en schrijft het
 *            resultaat weg;
 *   core 1 = checkercore: voert dezelfde berekening onafhankelijk uit en
 *            controleert de applicatiecore op DRIE punten:
 *              (1) INVOER    - CRC over het invoerblok (fout bij inlezen/transport)
 *              (2) VERWERKING - vergelijking van het rekenresultaat
 *              (3) UITVOER   - terugleescontrole van het weggeschreven resultaat
 *
 * Transport: gedeeld geheugen met het in 02/03 gevalideerde patroon
 * data -> memw -> vlag (spin-wait, geen FreeRTOS in het meetpad; queues zijn
 * daar ~60-95x trager, zie 02/03). FreeRTOS start enkel de twee taken op.
 *
 * Zelftest van de detector: in ronde ZELFTEST_RONDE corrumpeert de
 * applicatiecore BEWUST zijn resultaat (1 bitflip) na de berekening.
 * Verwacht: precies die ronde wordt gedetecteerd met verdict
 * VERWERKING|UITVOER (de corrupte waarde propageert naar de uitvoer);
 * alle andere rondes verdict 0. Een detector zonder bewezen detectie
 * is geen detector.
 *
 * Overheadmeting: na de lockstep-fase draait de applicatiecore dezelfde
 * workload nog eens ONBESCHERMD (zonder checker/handshake); het verschil in
 * cycli per ronde is de kern-overhead van de lockstep (nog zonder CoreMark).
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#define RONDES          1000
#define BLOK            32          /* woorden per invoerblok */
#define ZELFTEST_RONDE  500         /* bewuste corruptie door de app-core */
#define CPU_MHZ         240.0
#define MAX_FOUTLOG     8

#define FOUT_INVOER     (1u << 0)
#define FOUT_VERWERKING (1u << 1)
#define FOUT_UITVOER    (1u << 2)

typedef struct {
    volatile uint32_t invoer[BLOK]; /* app -> chk: invoerblok */
    volatile uint32_t crc_invoer;   /* app -> chk: CRC zoals de app het schreef */
    volatile uint32_t res_app;      /* app -> chk: rekenresultaat app-core */
    volatile uint32_t uitvoer;      /* app -> chk: "weggeschreven" resultaat */
    volatile uint32_t vlag_invoer;
    volatile uint32_t vlag_res;
    volatile uint32_t vlag_uit;
    volatile uint32_t vlag_klaar;   /* chk -> app: verdict beschikbaar */
    volatile uint32_t verdict;      /* bitmask FOUT_* van de checker */
} kanaal_t;

static kanaal_t k;
static uint32_t app_invoer[BLOK];   /* lokale kopie applicatiecore */
static uint32_t chk_invoer[BLOK];   /* lokale kopie checkercore */
static uint32_t cycli_beschermd[RONDES];
static uint32_t cycli_onbeschermd[RONDES];
static struct { uint32_t ronde; uint32_t verdict; } foutlog[MAX_FOUTLOG];
static volatile uint32_t n_fouten = 0;
static volatile uint32_t tel_invoer = 0, tel_verwerking = 0, tel_uitvoer = 0;

/* --- deterministische workload (identiek op beide cores) ----------------- */

static inline uint32_t xs32(uint32_t x)
{
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

/* Invoerblok voor een ronde: pseudo-random maar volledig bepaald door de
 * ronde-index, zodat beide cores exact weten wat er MOET staan. */
static void genereer_invoer(uint32_t ronde, uint32_t *buf)
{
    uint32_t s = ronde * 2654435761u + 1u;
    for (int i = 0; i < BLOK; i++) { s = xs32(s); buf[i] = s; }
}

static inline uint32_t crc_stap(uint32_t crc, uint32_t w)
{
    return (crc * 33u) ^ w;
}

/* De eigenlijke "berekening" van de applicatie: mix van rotate/xor/add over
 * het blok. Niet-triviaal genoeg dat een corrupte invoer vrijwel zeker een
 * ander resultaat geeft, goedkoop genoeg om de handshake-overhead niet te
 * verdrinken. */
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

/* --- statistiek (zelfde rapportagevorm als 03, met ronde-index) ---------- */

static void stats(const char *naam, const uint32_t *v, int n)
{
    uint32_t mn = UINT32_MAX, mx = 0; uint64_t som = 0; int imx = 0;
    for (int i = 0; i < n; i++) {
        som += v[i];
        if (v[i] < mn) mn = v[i];
        if (v[i] > mx) { mx = v[i]; imx = i; }
    }
    printf("%-24s min %6u / gem %8.1f / max %6u cycli (max in ronde %d)  (gem %.3f us)\n",
           naam, (unsigned)mn, (double)som / n, (unsigned)mx, imx, (som / (double)n) / CPU_MHZ);
}

static double gemiddelde(const uint32_t *v, int n)
{
    uint64_t som = 0;
    for (int i = 0; i < n; i++) som += v[i];
    return (double)som / n;
}

/* --- checkercore (core 1) ------------------------------------------------ */

static void taak_checker(void *arg)
{
    printf("[core %d] checkercore gestart\n", xPortGetCoreID());
    for (uint32_t ronde = 0; ronde < RONDES; ronde++) {
        uint32_t verdict = 0;

        /* (1) INVOER: wacht op het blok, lees het en controleer de CRC */
        while (k.vlag_invoer == 0) { }
        k.vlag_invoer = 0;
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) {
            chk_invoer[i] = k.invoer[i];
            crc = crc_stap(crc, chk_invoer[i]);
        }
        if (crc != k.crc_invoer) verdict |= FOUT_INVOER;

        /* (2) VERWERKING: zelfde berekening op de eigen kopie, vergelijk */
        uint32_t res_chk = bewerk(chk_invoer);
        while (k.vlag_res == 0) { }
        k.vlag_res = 0;
        if (k.res_app != res_chk) verdict |= FOUT_VERWERKING;

        /* (3) UITVOER: lees terug wat de app-core wegschreef */
        while (k.vlag_uit == 0) { }
        k.vlag_uit = 0;
        if (k.uitvoer != res_chk) verdict |= FOUT_UITVOER;

        /* verdict terug naar de applicatiecore */
        k.verdict = verdict;
        __asm__ __volatile__("memw");
        k.vlag_klaar = 1;
    }
    printf("[core %d] checkercore klaar (%d rondes gecontroleerd)\n",
           xPortGetCoreID(), RONDES);
    vTaskDelete(NULL);
}

/* --- applicatiecore (core 0) --------------------------------------------- */

static void taak_app(void *arg)
{
    printf("[core %d] applicatiecore gestart\n", xPortGetCoreID());

    /* FASE 1: beschermd (lockstep met checker) */
    for (uint32_t ronde = 0; ronde < RONDES; ronde++) {
        uint32_t t0 = esp_cpu_get_cycle_count();

        /* "inlezen": invoer genereren en delen met de checker (CRC mee) */
        genereer_invoer(ronde, app_invoer);
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) {
            k.invoer[i] = app_invoer[i];
            crc = crc_stap(crc, app_invoer[i]);
        }
        k.crc_invoer = crc;
        __asm__ __volatile__("memw");
        k.vlag_invoer = 1;

        /* "verwerken": eigen berekening op de eigen kopie */
        uint32_t res = bewerk(app_invoer);

        /* ZELFTEST: bewuste corruptie na de berekening (1 bitflip) */
        if (ronde == ZELFTEST_RONDE) res ^= 1u;

        k.res_app = res;
        __asm__ __volatile__("memw");
        k.vlag_res = 1;

        /* "wegschrijven": resultaat naar de uitvoerlocatie */
        k.uitvoer = res;
        __asm__ __volatile__("memw");
        k.vlag_uit = 1;

        /* wacht op het verdict van de checker */
        while (k.vlag_klaar == 0) { }
        uint32_t verdict = k.verdict;
        k.vlag_klaar = 0;

        uint32_t t1 = esp_cpu_get_cycle_count();
        cycli_beschermd[ronde] = t1 - t0;

        if (verdict != 0) {
            if (verdict & FOUT_INVOER)     tel_invoer++;
            if (verdict & FOUT_VERWERKING) tel_verwerking++;
            if (verdict & FOUT_UITVOER)    tel_uitvoer++;
            if (n_fouten < MAX_FOUTLOG) {
                foutlog[n_fouten].ronde = ronde;
                foutlog[n_fouten].verdict = verdict;
            }
            n_fouten++;
        }
    }

    /* FASE 2: onbeschermd (zelfde workload, geen checker/handshake) */
    for (uint32_t ronde = 0; ronde < RONDES; ronde++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        genereer_invoer(ronde, app_invoer);
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) crc = crc_stap(crc, app_invoer[i]);
        uint32_t res = bewerk(app_invoer);
        (void)crc; (void)res;
        uint32_t t1 = esp_cpu_get_cycle_count();
        cycli_onbeschermd[ronde] = t1 - t0;
    }

    /* --- rapportage ------------------------------------------------------ */
    printf("\n=== RESULTATEN lockstep-kern (%d rondes, blok %d woorden, 8 aug 2026) ===\n",
           RONDES, BLOK);
    printf("mismatch-tellers: invoer %u / verwerking %u / uitvoer %u  (rondes met fout: %u)\n",
           (unsigned)tel_invoer, (unsigned)tel_verwerking, (unsigned)tel_uitvoer,
           (unsigned)n_fouten);
    for (uint32_t i = 0; i < n_fouten && i < MAX_FOUTLOG; i++) {
        printf("  gedetecteerd: ronde %u, verdict 0x%x%s%s%s\n",
               (unsigned)foutlog[i].ronde, (unsigned)foutlog[i].verdict,
               (foutlog[i].verdict & FOUT_INVOER)     ? " [INVOER]" : "",
               (foutlog[i].verdict & FOUT_VERWERKING) ? " [VERWERKING]" : "",
               (foutlog[i].verdict & FOUT_UITVOER)    ? " [UITVOER]" : "");
    }

    /* zelftest-oordeel: exact 1 foutronde, namelijk ZELFTEST_RONDE, met
     * verdict VERWERKING|UITVOER (corrupt resultaat propageert naar uitvoer)
     * en GEEN invoerfout. */
    int zelftest_ok = (n_fouten == 1 &&
                       foutlog[0].ronde == ZELFTEST_RONDE &&
                       foutlog[0].verdict == (FOUT_VERWERKING | FOUT_UITVOER));
    printf("zelftest detector (bewuste bitflip in ronde %d): %s\n",
           ZELFTEST_RONDE, zelftest_ok ? "GESLAAGD - exact 1 ronde gedetecteerd"
                                       : "GEFAALD - zie tellers hierboven!");

    printf("\n--- kosten per ronde ---\n");
    stats("beschermd (lockstep)", cycli_beschermd, RONDES);
    stats("onbeschermd", cycli_onbeschermd, RONDES);
    double gb = gemiddelde(cycli_beschermd, RONDES);
    double go = gemiddelde(cycli_onbeschermd, RONDES);
    printf("overhead lockstep-kern: %.1f cycli/ronde = %.1f%% t.o.v. onbeschermd\n",
           gb - go, 100.0 * (gb - go) / go);
    printf("(overhead = delen invoer + CRC + 4x handshake + wachten op verdict;\n");
    printf(" de checker rekent parallel, dus de netto wachttijd hangt af van de\n");
    printf(" verhouding rekenwerk/transport bij deze blokgrootte)\n");
    printf("BEWIJS: alle tellers en cycli komen uit deze run op het bord; zie log met datum.\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("=== 05_lockstep_kern: app-core (0) + checker-core (1), 3 controlepunten ===\n");
    xTaskCreatePinnedToCore(taak_checker, "chk", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_app, "app", 16384, NULL, 5, NULL, 0);
}
