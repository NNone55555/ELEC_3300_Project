#ifndef MOUSE_APP_H
#define MOUSE_APP_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* After osKernelInitialize, before osKernelStart. */
bool MouseApp_Init(SPI_HandleTypeDef *optical_spi, UART_HandleTypeDef *fingerprint_uart,
                   UART_HandleTypeDef *console_uart, I2C_HandleTypeDef *profile_i2c);
void MouseApp_RunDisplay(void);
void MouseApp_SampleInputs1ms(void);
bool MouseApp_SetDpi(uint16_t dpi);
uint16_t MouseApp_GetDpi(void);

#endif
