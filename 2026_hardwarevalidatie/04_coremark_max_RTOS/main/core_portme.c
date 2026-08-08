/* core_portme.c — ESP32-S3-port voor officiële EEMBC CoreMark.
 * Twee meetmodi (instelbaar via coremark_minimal_mode):
 *   0 = RTOS actief: normale FreeRTOS-taak, ticks/interrupts aan
 *   1 = minimaal: scheduler geschorst + interrupts uit tijdens de gemeten lus
 */
#include <stdio.h>
#include "coremark.h"
#include "core_portme.h"
#include "esp_timer.h"
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

ee_u32 default_num_contexts = 1;

int coremark_minimal_mode = 0;   /* gezet door de wrapper vóór elke pass */

static CORE_TICKS t_start, t_stop;

void start_time(void)
{
    if (coremark_minimal_mode) {
        vTaskSuspendAll();
        portDISABLE_INTERRUPTS();
    }
    t_start = (CORE_TICKS)esp_timer_get_time();
}

void stop_time(void)
{
    t_stop = (CORE_TICKS)esp_timer_get_time();
    if (coremark_minimal_mode) {
        portENABLE_INTERRUPTS();
        xTaskResumeAll();
    }
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
