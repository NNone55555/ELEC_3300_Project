#ifndef NRF_MOTION_H
#define NRF_MOTION_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

typedef enum {
  NRF_MOTION_TX,
  NRF_MOTION_RX
} NrfMotionRole;

typedef enum {
  NRF_TX_UNAVAILABLE,
  NRF_TX_READY,
  NRF_TX_PENDING,
  NRF_TX_ACKED,
  NRF_TX_NO_ACK,
  NRF_TX_ERROR
} NrfMotionTxState;

typedef enum {
  NRF_RX_EMPTY,
  NRF_RX_PACKET,
  NRF_RX_ERROR
} NrfMotionRxResult;

/* One radio per board. Call after GPIO/SPI initialization, before regular work. */
bool NrfMotion_Init(SPI_HandleTypeDef *spi, NrfMotionRole role);

/* TX uses one payload at a time. Poll from the SPI owner until it completes. */
bool NrfMotion_StartTx(int16_t dx, int16_t dy);
void NrfMotion_PollTx(void);
NrfMotionTxState NrfMotion_GetTxState(void);

/* RX reads one packet if the radio's FIFO contains data. */
NrfMotionRxResult NrfMotion_Read(int16_t *dx, int16_t *dy);

#endif
