
// main.c
// Startpunt van het lockstep testproject op de ESP32 met FreeRTOS taken op Core 0 en Core 1

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lockstep_coremark.h"

void app_main(void) {
    printf("🔧 Lockstep demo gestart op ESP32 (FreeRTOS multicore)\n");

    // Start applicatie op Core 0
    xTaskCreatePinnedToCore(task_core0_main, "Task_Core0", 4096, NULL, 5, NULL, 0);

    // Start checker op Core 1
    xTaskCreatePinnedToCore(task_core1_checker, "Task_Core1", 4096, NULL, 5, NULL, 1);
}
void task_core0_main(void *arg)
{
    while (1) {
        printf("Core 0 taak actief!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_core1_checker(void *arg)
{
    while (1) {
        printf("Core 1 checker actief!\n");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}