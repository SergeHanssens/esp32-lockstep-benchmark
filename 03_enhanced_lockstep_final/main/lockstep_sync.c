
// lockstep_sync.c
// Synchronisatie tussen Core0 en Core1 met Queue

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lockstep_sync.h"

static QueueHandle_t sync_q = NULL;

void init_sync_method(sync_method_t method) {
    if (!sync_q && method == SYNC_METHOD_QUEUE) {
        sync_q = xQueueCreate(1, sizeof(uint32_t));
    }
}

void sync_send(uint32_t val) {
    xQueueOverwrite(sync_q, &val);
}

uint32_t sync_receive(void) {
    uint32_t val = 0;
    xQueuePeek(sync_q, &val, portMAX_DELAY);
    return val;
}
