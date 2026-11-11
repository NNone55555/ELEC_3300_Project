#ifndef AT24C02_H
#define AT24C02_H
#include "stm32f1xx_hal.h"
#include <stdbool.h>

#define AT24C02_SIZE 256U
typedef struct
{
  const char *operation;
  uint16_t address;
  HAL_StatusTypeDef hal_status;
  uint32_t hal_error;
  bool scl_high, sda_high, bus_busy;
} At24c02Diagnostic;

void At24c02_Init(I2C_HandleTypeDef *i2c);
/* Display/config task only, after scheduler start: it owns both EEPROM and LCD.
 * Transactions gate FSMC for the PB7 erratum and yield during STOP/ACK waits.
 */
bool At24c02_Read(uint16_t address, uint8_t *data, uint16_t length);
bool At24c02_WriteVerified(uint16_t address, const uint8_t *data, uint16_t length);
/* Failure snapshot captured before restoring FSMC; no bus reset or write. */
At24c02Diagnostic At24c02_GetDiagnostic(void);
#endif
