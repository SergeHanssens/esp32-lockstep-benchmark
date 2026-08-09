
// main.c
// Startpunt van het enhanced lockstep benchmark project op ESP32-S3

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lockstep_task.h"
#include "metadata.h"

void app_main(void) {
    printf("\n🔧 Enhanced Lockstep Benchmark gestart op ESP32-S3\n");
    print_board_metadata();

    // Start benchmark taken op beide cores
    xTaskCreatePinnedToCore(task_core0_main, "Core0_Benchmark", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_core1_checker, "Core1_Checker", 4096, NULL, 5, NULL, 1);
}
