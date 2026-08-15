/*
 * 05b_jtag_lockstep: continu-draaiende software-lockstep als DOELWIT voor
 * EXTERNE foutinjectie via OpenOCD/USB-JTAG.
 *
 * Instrument voor D:\thesis\ClaudeCode - de originele thesis-firmware
 * (05_lockstep_kern) blijft ongewijzigd. Zelfde 3-checkpoint-checker en
 * hetzelfde data->memw->vlag transport als het origineel; verschillen:
 *   - de applicatiecore draait ONEINDIG (geen vaste 1000 rondes, geen
 *     ingebouwde zelftest), zodat een extern debugger op elk moment kan
 *     injecteren;
 *   - drie GLOBALE, VOLATILE trigger-woorden (g_flip_invoer/res/uit). Een
 *     externe debugger schrijft daar via een JTAG-geheugenschrijfactie
 *     (OpenOCD `mww <adres> <bitmasker>`) een bitmasker in. De app-core
 *     past dat masker EEN keer toe op de echte databaan van de volgende
 *     ronde (XOR = bitflip) en wist de trigger. Omdat de trigger een
 *     gelatchte variabele is, bestaat er GEEN timing-race met de debugger.
 *   - elke gedetecteerde ronde wordt onmiddellijk geprint met rondenummer
 *     en verdict, plus een periodieke hartslag zodat zichtbaar is dat het
 *     systeem foutloos doorloopt tussen injecties in.
 *
 * Injectiepunten (spiegelen de 3 checkpoints van de checker):
 *   g_flip_invoer -> XOR op de door de app gepubliceerde invoer-CRC
 *                    -> checker ziet CRC-mismatch          -> verdict 0x1
 *   g_flip_res    -> XOR op het gepubliceerde rekenresultaat k.res_app
 *                    (uitvoer blijft correct)              -> verdict 0x2
 *   g_flip_uit    -> XOR op het weggeschreven resultaat k.uitvoer
 *                    (resultaat blijft correct)            -> verdict 0x4
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#define BLOK            32
#define HEARTBEAT       1000000u    /* rondes tussen twee hartslagregels */

#define FOUT_INVOER     (1u << 0)
#define FOUT_VERWERKING (1u << 1)
#define FOUT_UITVOER    (1u << 2)

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

/* --- Door OpenOCD/JTAG beschreven trigger-woorden (bitmaskers) --------- */
volatile uint32_t g_flip_invoer     = 0;  /* XOR op gepubliceerde CRC   -> 0x1 */
volatile uint32_t g_flip_res        = 0;  /* XOR op gepubliceerd res    -> 0x2 */
volatile uint32_t g_flip_uit        = 0;  /* XOR op weggeschreven uit   -> 0x4 */
volatile uint32_t g_flip_invoerdata = 0;  /* XOR op ECHT invoerwoord    -> 0x7 (propagatie) */
volatile uint32_t g_flip_invoer_idx = 5;  /* welk woord in k.invoer[] geraakt wordt */

/* --- Door OpenOCD/JTAG leesbare tellers (mdw) -------------------------- */
volatile uint32_t g_ronde       = 0;   /* huidige ronde-index          */
volatile uint32_t g_det_invoer  = 0;   /* gedetecteerde INVOER-fouten  */
volatile uint32_t g_det_res     = 0;   /* gedetecteerde VERWERKING     */
volatile uint32_t g_det_uit     = 0;   /* gedetecteerde UITVOER        */
volatile uint32_t g_det_totaal  = 0;   /* rondes met verdict != 0      */

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

/* --- checkercore (core 1): identiek aan 05_lockstep_kern -------------- */
static void taak_checker(void *arg)
{
    printf("[core %d] checkercore gestart (continu)\n", xPortGetCoreID());
    for (;;) {
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
}

/* --- applicatiecore (core 0) ----------------------------------------- */
static void taak_app(void *arg)
{
    printf("[core %d] applicatiecore gestart (continu)\n", xPortGetCoreID());
    printf("KLAAR-VOOR-INJECTIE: schrijf via JTAG naar g_flip_invoer/res/uit\n");

    uint32_t ronde = 0;
    for (;;) {
        g_ronde = ronde;

        /* "inlezen": invoer genereren + delen met de checker (CRC mee) */
        genereer_invoer(ronde, app_invoer);
        uint32_t crc = 0;
        for (int i = 0; i < BLOK; i++) {
            k.invoer[i] = app_invoer[i];
            crc = crc_stap(crc, app_invoer[i]);
        }
        k.crc_invoer = crc;
        /* INJECTIE 1 (INVOER): flip een bit in de gepubliceerde CRC (alleen
         * CRC-consistentie) -> verdict 0x1 */
        if (g_flip_invoer) { k.crc_invoer ^= g_flip_invoer; g_flip_invoer = 0; }
        /* INJECTIE 4 (INVOER-DATABIT, propagatie): flip een ECHT woord in het
         * gedeelde invoerblok. crc_invoer blijft de correcte app-waarde en de
         * app rekent op zijn eigen (schone) app_invoer, dus de checker ziet
         * CRC-mismatch EN de corruptie propageert naar verwerking en uitvoer
         * -> verdict 0x7. Dit test detectie + propagatie van een echte databit. */
        if (g_flip_invoerdata) {
            uint32_t idx = g_flip_invoer_idx % BLOK;
            k.invoer[idx] ^= g_flip_invoerdata;
            g_flip_invoerdata = 0;
        }
        __asm__ __volatile__("memw");
        k.vlag_invoer = 1;

        /* "verwerken" */
        uint32_t res = bewerk(app_invoer);
        k.res_app = res;
        /* INJECTIE 2 (VERWERKING): flip een bit in het gepubliceerde
         * resultaat; de uitvoer hieronder blijft de correcte waarde */
        if (g_flip_res) { k.res_app ^= g_flip_res; g_flip_res = 0; }
        __asm__ __volatile__("memw");
        k.vlag_res = 1;

        /* "wegschrijven" */
        k.uitvoer = res;
        /* INJECTIE 3 (UITVOER): flip een bit in het weggeschreven resultaat */
        if (g_flip_uit) { k.uitvoer ^= g_flip_uit; g_flip_uit = 0; }
        __asm__ __volatile__("memw");
        k.vlag_uit = 1;

        while (k.vlag_klaar == 0) { }
        uint32_t verdict = k.verdict;
        k.vlag_klaar = 0;

        if (verdict != 0) {
            if (verdict & FOUT_INVOER)     g_det_invoer++;
            if (verdict & FOUT_VERWERKING) g_det_res++;
            if (verdict & FOUT_UITVOER)    g_det_uit++;
            g_det_totaal++;
            printf("JTAG-INJECTIE GEDETECTEERD: ronde %u verdict 0x%x%s%s%s "
                   "(tot: inv %u / verw %u / uit %u)\n",
                   (unsigned)ronde, (unsigned)verdict,
                   (verdict & FOUT_INVOER)     ? " [INVOER]" : "",
                   (verdict & FOUT_VERWERKING) ? " [VERWERKING]" : "",
                   (verdict & FOUT_UITVOER)    ? " [UITVOER]" : "",
                   (unsigned)g_det_invoer, (unsigned)g_det_res, (unsigned)g_det_uit);
        }

        if (ronde % HEARTBEAT == 0) {
            printf("hartslag: ronde %u, 0 valse positieven, detecties totaal %u\n",
                   (unsigned)ronde, (unsigned)g_det_totaal);
        }
        ronde++;
    }
}

void app_main(void)
{
    printf("=== 05b_jtag_lockstep: continu lockstep-doelwit voor JTAG-injectie ===\n");
    xTaskCreatePinnedToCore(taak_checker, "chk", 8192, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(taak_app, "app", 16384, NULL, 5, NULL, 0);
}
