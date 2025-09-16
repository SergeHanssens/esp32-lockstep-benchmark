
// lockstep_sync.c
// Simpele Queue-gebaseerde synchronisatie tussen Core 0 en Core 1

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lockstep_sync.h"

static QueueHandle_t sync_queue = NULL;

void sync_with_queue_send(uint32_t value) {
    if (!sync_queue) {
        sync_queue = xQueueCreate(1, sizeof(uint32_t));
    }
    xQueueOverwrite(sync_queue, &value);
}

uint32_t sync_with_queue_receive(void) {
    uint32_t value = 0;
    if (sync_queue) {
        xQueuePeek(sync_queue, &value, portMAX_DELAY);
    }
    return value;
}
