#ifndef UART_TEST_H
#define UART_TEST_H

#include "stm32f1xx_hal.h"

void UART_Test_Init(UART_HandleTypeDef *uart);
void UART_Test_Poll(void);

#endif
