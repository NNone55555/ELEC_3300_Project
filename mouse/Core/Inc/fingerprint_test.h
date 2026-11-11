#ifndef FINGERPRINT_TEST_H
#define FINGERPRINT_TEST_H

#include "stm32f1xx_hal.h"

void Fingerprint_Test_Init(UART_HandleTypeDef *sensor, UART_HandleTypeDef *console);
void Fingerprint_Test_Poll(void);

#endif
