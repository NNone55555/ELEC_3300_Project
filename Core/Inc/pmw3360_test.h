#ifndef PMW3360_TEST_H
#define PMW3360_TEST_H

#include "stm32f1xx_hal.h"

/* SPI identification, SROM 0x04 upload, and 800 CPI X/Y motion test. */
void PMW3360_Test_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *uart);
void PMW3360_Test_Poll(void);

#endif
