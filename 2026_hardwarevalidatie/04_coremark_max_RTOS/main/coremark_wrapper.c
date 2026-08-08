/* 04_coremark_max_RTOS: EEMBC CoreMark als aparte firmware, VOLLEDIG onder RTOS.
 * Normale FreeRTOS-taak, ticks + interrupts + watchdogs AAN (standaardconfig).
 * Kruisvalidatie t.o.v. pass A van 04_coremark. */
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
    printf("\n=== 04_coremark_max_RTOS: EEMBC CoreMark, VOLLEDIG RTOS (aparte firmware), core %d, CPU %lu MHz ===\n",
           xPortGetCoreID(), (unsigned long)(freq_hz / 1000000));
    coremark_minimal_mode = 0;
    coremark_main();
    printf("\n=== KLAAR (max_RTOS) ===\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(coremark_taak, "coremark", 32768, NULL, 5, NULL, 0);
}
