/* Wrapper: draait de officiële EEMBC CoreMark twee keer op core 0.
 *   Pass A: RTOS actief (normale taak, vergelijkbaar met referentiescores)
 *   Pass B: minimaal (scheduler geschorst + interrupts uit in de meetlus)
 * Masterproef Serge Hanssens — baseline single-core, 8 aug 2026.
 */
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
    printf("\n=== EEMBC CoreMark baseline ESP32-S3, single-core (core %d), CPU %lu MHz ===\n",
           xPortGetCoreID(), (unsigned long)(freq_hz / 1000000));

    printf("\n--- PASS A: RTOS actief (ticks + interrupts aan) ---\n");
    coremark_minimal_mode = 0;
    coremark_main();

    printf("\n--- PASS B: minimaal (scheduler geschorst, interrupts uit in meetlus) ---\n");
    coremark_minimal_mode = 1;
    coremark_main();

    printf("\n=== KLAAR: beide passes uitgevoerd. Log dit bestand als baseline-bewijs. ===\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(coremark_taak, "coremark", 32768, NULL, 5, NULL, 0);
}
