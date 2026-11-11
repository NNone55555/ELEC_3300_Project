#ifndef ENCODER_TEST_H
#define ENCODER_TEST_H

#include "stm32f1xx_hal.h"

void Encoder_Test_Init(UART_HandleTypeDef *uart);
void Encoder_Test_Sample(void); /* Call from the existing 1 ms TIM4 callback. */
void Encoder_Test_Poll(void);   /* Call from defaultTask for LCD/UART output. */

#endif
