
#ifndef LOCKSTEP_SYNC_H
#define LOCKSTEP_SYNC_H

#include <stdint.h>

typedef enum {
    SYNC_METHOD_QUEUE = 0
} sync_method_t;

void init_sync_method(sync_method_t method);
void sync_send(uint32_t val);
uint32_t sync_receive(void);

#endif
