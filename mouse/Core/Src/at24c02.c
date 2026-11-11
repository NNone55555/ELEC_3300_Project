#include "at24c02.h"
#include "cmsis_os2.h"
#include <string.h>

#define EEPROM_ADDRESS (0x50U << 1)
#define PAGE_BYTES 8U
static I2C_HandleTypeDef *bus;
static At24c02Diagnostic diagnostic;
static void RecordFailure(const char *operation, uint16_t address, HAL_StatusTypeDef result)
{
  diagnostic.operation = operation;
  diagnostic.address = address;
  diagnostic.hal_status = result;
  diagnostic.hal_error = bus != NULL ? HAL_I2C_GetError(bus) : 0U;
  diagnostic.scl_high = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET;
  diagnostic.sda_high = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET;
  diagnostic.bus_busy = bus != NULL && (bus->Instance->SR2 & I2C_SR2_BUSY) != 0U;
}
At24c02Diagnostic At24c02_GetDiagnostic(void)
{
  return diagnostic;
}
/* Restored from the working EEPROM test in commit e7503f3.
 * ES0340 section 2.3.9: FSMC/NADV conflicts with I2C1 SDA on PB7.
 * The display/config task is the sole LCD and EEPROM owner. No other task
 * or ISR may access the FSMC LCD while an EEPROM operation is in progress.
 * Never mask interrupts here: USB, fingerprint and HAL TIM4 must keep running.
 */
static uint32_t PauseLCD(void)
{
  uint32_t enabled = RCC->AHBENR & RCC_AHBENR_FSMCEN;
  __HAL_RCC_FSMC_CLK_DISABLE();
  __DSB();
  return enabled;
}
static void ResumeLCD(uint32_t enabled)
{
  if (enabled != 0U)
  {
    __HAL_RCC_FSMC_CLK_ENABLE();
    __DSB();
  }
}
static HAL_StatusTypeDef FinishTransfer(HAL_StatusTypeDef result, const char *operation,
                                        uint16_t address)
{
  /* HAL memory calls can return with STOP still pending. Keep FSMC gated
   * until the peripheral has actually released the bus, with a bounded wait.
   */
  if (result == HAL_OK)
  {
    uint32_t started = HAL_GetTick();
    while ((bus->Instance->CR1 & I2C_CR1_STOP) != 0U || (bus->Instance->SR2 & I2C_SR2_BUSY) != 0U)
    {
      if ((uint32_t)(HAL_GetTick() - started) >= 10U)
      {
        RecordFailure("STOP/bus idle timeout", address, HAL_TIMEOUT);
        return HAL_TIMEOUT;
      }
      osDelay(1);
    }
  }
  if (result != HAL_OK)
    RecordFailure(operation, address, result);
  return result;
}
void At24c02_Init(I2C_HandleTypeDef *i2c)
{
  bus = i2c;
  diagnostic = (At24c02Diagnostic){.operation = "init", .hal_status = HAL_ERROR};
}
bool At24c02_Read(uint16_t address, uint8_t *data, uint16_t length)
{
  if (bus == NULL || address > AT24C02_SIZE || length > AT24C02_SIZE - address ||
      (data == NULL && length != 0U))
    return false;
  if (length == 0U)
    return true;
  uint32_t lcd_clock = PauseLCD();
  HAL_StatusTypeDef result =
      HAL_I2C_Mem_Read(bus, EEPROM_ADDRESS, address, I2C_MEMADD_SIZE_8BIT, data, length, 50);
  result = FinishTransfer(result, "read", address);
  ResumeLCD(lcd_clock);
  return result == HAL_OK;
}
bool At24c02_WriteVerified(uint16_t address, const uint8_t *data, uint16_t length)
{
  if (bus == NULL || address > AT24C02_SIZE || length > AT24C02_SIZE - address ||
      (data == NULL && length != 0U))
    return false;
  if (length == 0U)
    return true;
  bool success = false;
  /* Includes page writes, programming ACK polls and nested verified reads. */
  uint32_t lcd_clock = PauseLCD();
  while (length != 0U)
  {
    uint16_t part = PAGE_BYTES - address % PAGE_BYTES;
    if (part > length)
      part = length;
    HAL_StatusTypeDef result = HAL_I2C_Mem_Write(bus, EEPROM_ADDRESS, address, I2C_MEMADD_SIZE_8BIT,
                                                 (uint8_t *)data, part, 20);
    if (FinishTransfer(result, "write", address) != HAL_OK)
      goto done;
    uint32_t started = HAL_GetTick();
    while (FinishTransfer(HAL_I2C_IsDeviceReady(bus, EEPROM_ADDRESS, 1, 2), "ACK poll", address) !=
           HAL_OK)
    {
      if ((uint32_t)(HAL_GetTick() - started) >= 10U)
      {
        goto done;
      }
      osDelay(1); /* EEPROM write-cycle ACK polling yields to mouse tasks. */
    }
    uint8_t verify[PAGE_BYTES];
    if (!At24c02_Read(address, verify, part))
      goto done;
    if (memcmp(data, verify, part) != 0)
    {
      RecordFailure("readback mismatch", address, HAL_OK);
      goto done;
    }
    address += part;
    data += part;
    length -= part;
  }
  success = true;
done:
  ResumeLCD(lcd_clock);
  return success;
}
