/* core_portme.c — ESP32-S3-port voor officiële EEMBC CoreMark, variant 07:
 * softwarematige checkpoint-lockstep.
 *
 * Architectuur (identiek aan 05_lockstep_kern, maar met CoreMark als workload):
 *   context 0 = applicatiecore (core 0): draait CoreMark-iteraties inline;
 *   context 1 = checkercore   (core 1): draait dezelfde iteraties onafhankelijk
 *               op een eigen datablok en vergelijkt na elk segment de vier
 *               CRC's (crcfinal/crclist/crcmatrix/crcstate) via het in 02/03/05
 *               gevalideerde shared-memory-patroon: data -> memw -> vlag
 *               (spin-wait, geen FreeRTOS-primitieven in het meetpad).
 *
 * Het checkpoint (publiceren + vergelijken + verdict) gebeurt BINNEN de door
 * CoreMark getimede regio (tussen start_time en stop_time, in core_stop_parallel),
 * zodat de checkpointkost integraal in de gemeten segmentduur zit.
 *
 * Zelftest naar 05-voorbeeld: in segment ls_selftest_segment publiceert de
 * applicatiecore bewust een gecorrumpeerde crcfinal (1 bitflip op de
 * GEPUBLICEERDE waarde; de berekening zelf blijft onaangetast). Verwacht:
 * precies dat segment krijgt verdict LS_FOUT_CRCFINAL.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "coremark.h"
#include "core_portme.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0xe9f5;
volatile ee_s32 seed2_volatile = 0x18f2;
volatile ee_s32 seed3_volatile = 0x2;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

ee_u32 default_num_contexts = 1;   /* wrapper zet 1 (onbeschermd) of 2 (lockstep) */

void *iterate(void *pres);         /* gedefinieerd in core_main.c (niet static) */

static CORE_TICKS t_start, t_stop;
static void ls_start_calls_reset(void);

void start_time(void)
{
    ls_start_calls_reset();        /* zie verderop: contexttelling per coremark_main */
    t_start = (CORE_TICKS)esp_timer_get_time();
}

void stop_time(void)
{
    t_stop = (CORE_TICKS)esp_timer_get_time();
    /* segmentduur loggen en naar het volgende segment */
    if (ls_segment_index < LS_MAX_SEG)
        ls_log_ticks_us[ls_segment_index] = (uint32_t)(t_stop - t_start);
    ls_segment_index++;
}

CORE_TICKS get_time(void)
{
    return t_stop - t_start;
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    return ((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
}

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc; (void)argv;
    p->portable_id = 1;
}

void portable_fini(core_portable *p)
{
    p->portable_id = 0;
}

/* ---- ee_printf: doorlaten of onderdrukken (met ERROR-detectie) ---------- */

volatile int ls_quiet = 0;
volatile uint32_t ls_error_count = 0;
char ls_eerste_error[128];

int lockstep_ee_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* ERROR-regels altijd tellen; de 10s-runtimemelding is een verwacht
     * artefact van segmentatie (korte segmenten) en telt NIET mee. */
    if (strstr(buf, "ERROR") != NULL && strstr(buf, "at least 10 secs") == NULL) {
        if (ls_error_count == 0) {
            strncpy(ls_eerste_error, buf, sizeof ls_eerste_error - 1);
            ls_eerste_error[sizeof ls_eerste_error - 1] = '\0';
        }
        ls_error_count++;
    }
    if (!ls_quiet)
        fputs(buf, stdout);
    return n;
}

/* ---- lockstep-kanaal en logboek (patroon uit 02/03/05) ------------------ */

typedef struct {
    /* app -> chk: gepubliceerde CRC's van de applicatiecore */
    volatile uint32_t crc_final, crc_list, crc_matrix, crc_state;
    volatile uint32_t vlag_app;      /* app -> chk: CRC's staan klaar */
    volatile uint32_t verdict;       /* chk -> app: bitmask LS_FOUT_* */
    volatile uint32_t vlag_klaar;    /* chk -> app: verdict beschikbaar */
    /* fasebesturing (buiten de getimede regio) */
    volatile uint32_t arm;                   /* start(ctx1): segment starten */
    core_results * volatile chk_res;         /* contextpointer voor de checker */
    volatile uint32_t fase_stop;             /* wrapper: fase afgelopen */
    volatile uint32_t worker_slaapt;         /* checker: terug in slaapstand */
} ls_kanaal_t;

static ls_kanaal_t k;

volatile int ls_selftest_segment = -1;
volatile uint32_t ls_segment_index = 0;
uint32_t ls_log_ticks_us[LS_MAX_SEG];
uint32_t ls_log_latentie_cyc[LS_MAX_SEG];
uint32_t ls_log_verdict[LS_MAX_SEG];

static int ls_start_calls = 0;               /* contexttelling per coremark_main */
static core_results *ls_app_res = NULL;
static uint32_t ls_t0_checkpoint = 0;        /* cyclusteller app-core bij publicatie */
static TaskHandle_t ls_worker = NULL;

static void ls_start_calls_reset(void) { ls_start_calls = 0; }

/* ---- checkertaak (core 1) ----------------------------------------------- */

/* Buiten beschermde fasen slaapt de checker geblokkeerd (taaknotificatie),
 * zodat core 1 tijdens de onbeschermde referentiefasen volledig stil is en
 * geen busverkeer veroorzaakt. Binnen een fase draait hij een strakke
 * spin-lus op het arm-vlagje (RTOS-vrij meetpad, zoals 05). */
static void ls_taak_checker(void *arg)
{
    (void)arg;
    printf("[core %d] checkercore gestart (slaapstand)\n", xPortGetCoreID());
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* wacht op fasestart */
        while (!k.fase_stop) {
            if (k.arm) {
                k.arm = 0;
                core_results *res = k.chk_res;
                iterate(res);                          /* eigen, onafhankelijke uitvoering */
                uint32_t verdict = 0;
                while (k.vlag_app == 0) { }            /* wacht op publicatie app */
                k.vlag_app = 0;
                if (k.crc_final  != res->crc)       verdict |= LS_FOUT_CRCFINAL;
                if (k.crc_list   != res->crclist)   verdict |= LS_FOUT_CRCLIST;
                if (k.crc_matrix != res->crcmatrix) verdict |= LS_FOUT_CRCMATRIX;
                if (k.crc_state  != res->crcstate)  verdict |= LS_FOUT_CRCSTATE;
                k.verdict = verdict;
                __asm__ __volatile__("memw");
                k.vlag_klaar = 1;
            }
        }
        k.fase_stop = 0;
        k.worker_slaapt = 1;
    }
}

void ls_worker_start(void)
{
    if (ls_worker == NULL)
        xTaskCreatePinnedToCore(ls_taak_checker, "ls_chk", 16384, NULL, 5,
                                &ls_worker, 1);
}

void ls_fase_start(void)
{
    k.arm = 0; k.vlag_app = 0; k.vlag_klaar = 0; k.fase_stop = 0;
    k.worker_slaapt = 0;
    __asm__ __volatile__("memw");
    xTaskNotifyGive(ls_worker);               /* checker naar de hete lus */
}

void ls_fase_stop(void)
{
    k.fase_stop = 1;
    __asm__ __volatile__("memw");
    while (k.worker_slaapt == 0) { }          /* buiten de getimede regio */
}

void ls_reset_log(void)
{
    ls_segment_index = 0;
    ls_error_count = 0;
    ls_eerste_error[0] = '\0';
    memset(ls_log_ticks_us, 0, sizeof ls_log_ticks_us);
    memset(ls_log_latentie_cyc, 0, sizeof ls_log_latentie_cyc);
    memset(ls_log_verdict, 0, sizeof ls_log_verdict);
}

/* ---- EEMBC-parallelkoppeling (aangeroepen vanuit core_main.c) -----------
 * Volgorde in core_main.c: start(ctx0), start(ctx1), stop(ctx0), stop(ctx1),
 * alles tussen start_time() en stop_time().
 *   start(ctx0): applicatiecontext registreren (uitvoering volgt in stop(ctx0));
 *   start(ctx1): checker armen -> core 1 begint direct aan zijn iteraties;
 *   stop(ctx0) : applicatie-iteraties inline op core 0, daarna CRC's publiceren;
 *   stop(ctx1) : wachten op het verdict van de checker (= checkpoint klaar).
 * Zo draaien beide cores hun segment parallel en valt het volledige
 * checkpoint binnen de getimede regio. */

ee_u8 core_start_parallel(core_results *res)
{
    int n = ls_start_calls++;
    if (n == 0) {
        ls_app_res = res;
    } else {
        k.chk_res = res;
        __asm__ __volatile__("memw");
        k.arm = 1;
    }
    return 0;
}

ee_u8 core_stop_parallel(core_results *res)
{
    if (res == ls_app_res) {
        /* applicatiewerk: CoreMark-iteraties inline op core 0 */
        iterate(res);
        if (ls_start_calls == 2) {
            /* checkpoint: CRC's publiceren voor de checker.
             * Zelftest: bewuste bitflip op de GEPUBLICEERDE crcfinal. */
            uint32_t crc_pub = res->crc;
            if ((int)ls_segment_index == ls_selftest_segment)
                crc_pub ^= 1u;
            ls_t0_checkpoint = esp_cpu_get_cycle_count();
            k.crc_final  = crc_pub;
            k.crc_list   = res->crclist;
            k.crc_matrix = res->crcmatrix;
            k.crc_state  = res->crcstate;
            __asm__ __volatile__("memw");
            k.vlag_app = 1;
        }
    } else {
        /* wacht op het verdict van de checker (checkpoint afronden) */
        while (k.vlag_klaar == 0) { }
        k.vlag_klaar = 0;
        uint32_t t1 = esp_cpu_get_cycle_count();
        if (ls_segment_index < LS_MAX_SEG) {
            ls_log_latentie_cyc[ls_segment_index] = t1 - ls_t0_checkpoint;
            ls_log_verdict[ls_segment_index] = k.verdict;
        }
    }
    return 0;
}
