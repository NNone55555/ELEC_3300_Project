#ifndef CONSOLE_UART_H
#define CONSOLE_UART_H
#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stddef.h>

bool Console_Init(UART_HandleTypeDef *uart);
/* All-or-nothing bounded enqueue. ISR sends the immutable queued bytes. */
bool Console_Write(const char *text, size_t length);
void Console_Service(void);
/* 1 byte, 0 none, -1 RX error/overflow: discard until next newline. */
int Console_ReadByte(uint8_t *byte);
void Console_OnRxComplete(UART_HandleTypeDef *uart);
void Console_OnTxComplete(UART_HandleTypeDef *uart);
void Console_OnError(UART_HandleTypeDef *uart);
#endif
