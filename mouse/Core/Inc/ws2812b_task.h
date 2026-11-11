#ifndef WS2812B_TASK_H
#define WS2812B_TASK_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* After osKernelInitialize, before osKernelStart. Owns periodic driver Update.
 * The optional demonstration is independent of fingerprint/profile/serial code.
 */
bool WS2812B_TaskStart(TIM_HandleTypeDef *timer);

#endif
