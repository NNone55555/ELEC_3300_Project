#ifndef ZW3020_H
#define ZW3020_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

enum {
  ZW3020_OK = 0,
  ZW3020_NO_FINGER = 0x02,
  ZW3020_NO_MATCH = 0x09,
  ZW3020_EMPTY = 0x24,
  ZW3020_TIMEOUT = -1,
  ZW3020_BAD_PACKET = -2,
  ZW3020_UART_ERROR = -3,
  ZW3020_FULL = -4
};

typedef struct {
  uint16_t capacity;
  uint16_t count;
} Zw3020Info;

typedef struct {
  uint16_t id;
  uint16_t score;
} Zw3020Match;

/* One fingerprint task owns commands. The callbacks only fill its RX ring. */
bool Zw3020_Init(UART_HandleTypeDef *uart);
void Zw3020_OnRxComplete(UART_HandleTypeDef *uart);
void Zw3020_OnUartError(UART_HandleTypeDef *uart);
int16_t Zw3020_Connect(Zw3020Info *info);
int16_t Zw3020_GetImage(void);
int16_t Zw3020_Extract(uint8_t buffer);
int16_t Zw3020_Combine(void);
int16_t Zw3020_Store(uint16_t id);
int16_t Zw3020_Search(uint16_t capacity, Zw3020Match *match);
int16_t Zw3020_FindFreeSlot(uint16_t capacity, uint16_t *id);
uint8_t Zw3020_LastCommand(void);
int16_t Zw3020_Delete(uint16_t id);
int16_t Zw3020_IsOccupied(uint16_t id, bool *occupied);

#endif
