#ifndef BOOT_EPOCH_H
#define BOOT_EPOCH_H

#include <stdbool.h>
#include <stdint.h>

/* Call once before starting the radio or the scheduler. Zero means unavailable. */
bool BootEpoch_Reserve(void);
uint64_t BootEpoch_Value(void);

#endif
