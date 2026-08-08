/* 04_coremark_min_RTOS: EEMBC CoreMark als aparte firmware, MINIMAAL RTOS.
 * RTOS start enkel de taak; tijdens de gemeten lus: scheduler geschorst +
 * interrupts uit (watchdogs uit via sdkconfig, anders reset).
 * Kruisvalidatie t.o.v. pass B van 04_coremark. */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_clk_tree.h"
#include "soc/soc_caps.h"

extern int coremark_minimal_mode;
int coremark_main(void);

static void coremark_taak(void *arg)
{
    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &freq_hz);
    printf("\n=== 04_coremark_min_RTOS: EEMBC CoreMark, MINIMAAL RTOS (aparte firmware), core %d, CPU %lu MHz ===\n",
           xPortGetCoreID(), (unsigned long)(freq_hz / 1000000));
    coremark_minimal_mode = 1;
    coremark_main();
    printf("\n=== KLAAR (min_RTOS) ===\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(coremark_taak, "coremark", 32768, NULL, 5, NULL, 0);
}
