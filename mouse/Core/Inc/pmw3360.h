#ifndef PMW3360_H
#define PMW3360_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

typedef enum {
  PMW3360_OK,
  PMW3360_STARTING,
  PMW3360_SPI_ERROR,
  PMW3360_ID_ERROR,
  PMW3360_TIMER_ERROR,
  PMW3360_SROM_ERROR,
  PMW3360_CONFIG_ERROR
} Pmw3360Status;

typedef struct {
  int16_t dx;
  int16_t dy;
  uint8_t quality;
  bool lifted;
} Pmw3360Motion;

/* SPI2 has one task owner. Call these functions from the motion task. */
Pmw3360Status Pmw3360_Init(SPI_HandleTypeDef *spi, uint16_t cpi);
Pmw3360Status Pmw3360_ReadMotion(Pmw3360Motion *motion);
/* Motion-task owner only; updates CPI without resetting/uploading SROM. */
Pmw3360Status Pmw3360_SetCpi(uint16_t cpi);

#endif
