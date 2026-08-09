/* core_portme.h — ESP32-S3-port voor officiële EEMBC CoreMark, variant 07:
 * softwarematige checkpoint-lockstep. Gebaseerd op de 04-port; wijzigingen
 * t.o.v. 04 (allemaal porting layer, core_*.c blijft byte-identiek upstream):
 *   - MULTITHREAD 2 + eigen core_start_parallel/core_stop_parallel:
 *     context 0 = applicatiecore (core 0), context 1 = checkercore (core 1).
 *   - MEM_METHOD MEM_STACK i.p.v. MEM_STATIC: core_main.c staat MEM_STATIC
 *     niet toe bij meerdere contexts (#error in core_main.c); elke context
 *     krijgt zo een eigen datablok van TOTAL_DATA_SIZE bytes.
 *   - ee_printf omgeleid naar lockstep_ee_printf zodat de per-segment-
 *     rapporten onderdrukt kunnen worden (stille modus) met behoud van
 *     ERROR-detectie in de output.
 * Timing via esp_timer (gedeelde 52-bit hardwareteller, 1 us resolutie).
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stdint.h>
#include <stddef.h>

#define HAS_FLOAT 1
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 1
/* HAS_PRINTF 0: anders herdefinieert coremark.h ee_printf naar printf en
 * zou de stille modus (lockstep_ee_printf) omzeild worden. */
#define HAS_PRINTF 0

#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STACK

#ifndef TOTAL_DATA_SIZE
#define TOTAL_DATA_SIZE 2000
#endif

#ifndef ITERATIONS
#define ITERATIONS 20000
#endif

typedef uint8_t  ee_u8;
typedef uint16_t ee_u16;
typedef int16_t  ee_s16;
typedef int32_t  ee_s32;
typedef uint32_t ee_u32;
typedef float    ee_f32;
typedef uintptr_t ee_ptr_int;
typedef size_t   ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

typedef uint64_t CORE_TICKS;
#define EE_TICKS_PER_SEC 1000000

#ifndef COMPILER_VERSION
#define COMPILER_VERSION "GCC" __VERSION__
#endif
#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS FLAGS_STR
#endif
#ifndef MEM_LOCATION
#define MEM_LOCATION "STACK"
#endif

/* ee_printf omgeleid: stille modus onderdrukt de per-segment-rapporten,
 * maar ERROR-regels worden altijd geteld (en de eerste bewaard). */
int lockstep_ee_printf(const char *fmt, ...);
#define ee_printf lockstep_ee_printf

#define CRC_INIT 0xffff

extern ee_u32 default_num_contexts;
#define MULTITHREAD 2
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0
#define PARALLEL_METHOD "ESP32S3-checkpoint-lockstep"

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

/* ---- lockstep-uitbreidingen van de porting layer (voor de wrapper) ------ */

/* iteraties per coremark_main-aanroep: officiële runtime-parameter (seed 4);
 * coremark.h declareert deze niet extern, core_util.c leest hem via get_seed_32 */
extern volatile ee_s32 seed4_volatile;

#define LS_MAX_SEG 512          /* max. segmenten per fase (logbuffers) */

/* foutbits in het verdict van de checker (per checkpoint) */
#define LS_FOUT_CRCFINAL  (1u << 0)
#define LS_FOUT_CRCLIST   (1u << 1)
#define LS_FOUT_CRCMATRIX (1u << 2)
#define LS_FOUT_CRCSTATE  (1u << 3)

extern volatile int ls_quiet;            /* 1 = CoreMark-rapporten onderdrukken */
extern volatile int ls_selftest_segment; /* segmentindex met bewuste bitflip, -1 = uit */
extern volatile uint32_t ls_segment_index;   /* huidig segment binnen de fase */
extern volatile uint32_t ls_error_count;     /* ERROR-regels (excl. 10s-melding) */
extern char ls_eerste_error[128];            /* eerste onderdrukte ERROR-regel */

/* per-segment-logboek (gevuld tijdens de fase, wrapper leest na afloop) */
extern uint32_t ls_log_ticks_us[LS_MAX_SEG];     /* getimede duur (us, CoreMark-klok) */
extern uint32_t ls_log_latentie_cyc[LS_MAX_SEG]; /* checkpointlatentie app-core (cycli) */
extern uint32_t ls_log_verdict[LS_MAX_SEG];      /* verdict checker (0 = OK) */

void ls_worker_start(void);   /* eenmalig: checkertaak op core 1 aanmaken */
void ls_fase_start(void);     /* voor een beschermde fase: checker wekken */
void ls_fase_stop(void);      /* na een beschermde fase: checker laten slapen */
void ls_reset_log(void);      /* segmentindex + logbuffers + errorteller nul */

#endif /* CORE_PORTME_H */
