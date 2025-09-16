
#ifndef LOCKSTEP_SYNC_H
#define LOCKSTEP_SYNC_H

#include <stdint.h>

void sync_with_queue_send(uint32_t value);
uint32_t sync_with_queue_receive(void);

#endif
