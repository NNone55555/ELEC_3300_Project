#include "eeprom_test.h"
#include "lcd.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Schematic: A0/A1/A2 and WP grounded. HAL expects the shifted address. */
#define EEPROM_ADDRESS       (0x50U << 1)
#define EEPROM_TEST_OFFSET   0xF8U
#define EEPROM_RECORD_SIZE   8U
#define I2C_TIMEOUT_MS       100U
#define BUTTON_DEBOUNCE_MS   30U

static I2C_HandleTypeDef *test_i2c;
static int32_t counter;
static uint8_t test_ready;
static GPIO_PinState last_raw;
static GPIO_PinState stable_button;
static uint32_t button_changed_at;
static uint32_t diagnostic_error;
static uint32_t diagnostic_busy;
static GPIO_PinState diagnostic_scl;
static GPIO_PinState diagnostic_sda;
static const char *save_failure;
static uint8_t mismatch_offset;
static uint8_t mismatch_expected;
static uint8_t mismatch_actual;

static void CaptureI2CFailure(HAL_StatusTypeDef result)
{
  if (result != HAL_OK)
  {
    /* Sample before restoring FSMC, which may affect PB7 on this silicon. */
    diagnostic_error = HAL_I2C_GetError(test_i2c);
    diagnostic_busy = (test_i2c->Instance->SR2 & I2C_SR2_BUSY) != 0U;
    diagnostic_scl = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6);
    diagnostic_sda = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7);
  }
}

/* STM32F103xE errata ES0340 2.3.9: FSMC can conflict with I2C1 SDA
 * on PB7. No LCD access is permitted while its FSMC clock is gated.
 * This test is the sole LCD/I2C owner (defaultTask).
 */
static uint32_t PauseLCD(void)
{
  uint32_t was_enabled = RCC->AHBENR & RCC_AHBENR_FSMCEN;
  __HAL_RCC_FSMC_CLK_DISABLE();
  __DSB();
  return was_enabled;
}

static void ResumeLCD(uint32_t was_enabled)
{
  if (was_enabled != 0U)
  {
    __HAL_RCC_FSMC_CLK_ENABLE();
    __DSB();
  }
}

static HAL_StatusTypeDef FinishTransfer(HAL_StatusTypeDef result,
                                       uint32_t lcd_clock)
{
  /* The blocking HAL memory functions request STOP and can return before
   * it finishes. Keep FSMC gated until STOP is complete and BUSY clears.
   */
  if (result == HAL_OK)
  {
    uint32_t started = HAL_GetTick();
    while ((test_i2c->Instance->CR1 & I2C_CR1_STOP) != 0U
           || (test_i2c->Instance->SR2 & I2C_SR2_BUSY) != 0U)
    {
      if ((uint32_t)(HAL_GetTick() - started) >= 10U)
      {
        result = HAL_TIMEOUT;
        break;
      }
    }
  }
  CaptureI2CFailure(result);
  ResumeLCD(lcd_clock);
  return result;
}

static HAL_StatusTypeDef EEPROMReady(uint32_t trials, uint32_t timeout)
{
  uint32_t lcd_clock = PauseLCD();
  HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(test_i2c, EEPROM_ADDRESS,
                                                  trials, timeout);
  return FinishTransfer(result, lcd_clock);
}

static void ShowStatus(const char *message)
{
  LCD_Clear(0, 100, 240, HEIGHT_EN_CHAR, BACKGROUND);
  LCD_DrawString(0, 100, message);
}

static void ShowCounter(void)
{
  char text[30];
  (void)snprintf(text, sizeof(text), "Value: %ld", (long)counter);
  LCD_Clear(0, 60, 240, HEIGHT_EN_CHAR, BACKGROUND);
  LCD_DrawString(0, 60, text);
}

static HAL_StatusTypeDef ReadRecord(uint8_t *record)
{
  uint32_t lcd_clock = PauseLCD();
  HAL_StatusTypeDef result = HAL_I2C_Mem_Read(test_i2c, EEPROM_ADDRESS,
                          EEPROM_TEST_OFFSET, I2C_MEMADD_SIZE_8BIT, record,
                          EEPROM_RECORD_SIZE, I2C_TIMEOUT_MS);
  return FinishTransfer(result, lcd_clock);
}

static void ShowI2CFailure(const char *stage, HAL_StatusTypeDef result)
{
  char text[30];
  ShowStatus(stage);
  (void)snprintf(text, sizeof(text), "HAL=%u ERR=%08lX", (unsigned)result,
                 (unsigned long)diagnostic_error);
  LCD_DrawString(0, 180, text);
  (void)snprintf(text, sizeof(text), "SCL=%u SDA=%u BUSY=%lu",
                 (unsigned)diagnostic_scl, (unsigned)diagnostic_sda,
                 (unsigned long)diagnostic_busy);
  LCD_DrawString(0, 200, text);
}

static uint32_t DecodeValue(const uint8_t *record)
{
  return (uint32_t)record[0] | ((uint32_t)record[1] << 8)
         | ((uint32_t)record[2] << 16) | ((uint32_t)record[3] << 24);
}

static HAL_StatusTypeDef SaveCounter(int32_t value)
{
  uint8_t record[EEPROM_RECORD_SIZE];
  uint8_t readback[EEPROM_RECORD_SIZE];
  uint32_t encoded = (uint32_t)value;
  save_failure = "EEPROM write failed";

  /* Four little-endian bytes, followed by their bitwise complements. */
  for (uint32_t i = 0; i < 4U; ++i)
  {
    record[i] = (uint8_t)(encoded >> (8U * i));
    record[i + 4U] = (uint8_t)~record[i];
  }

  /* Keep FSMC off through write, programming, and verification. */
  uint32_t lcd_clock = PauseLCD();
  /* 0xF8..0xFF is one aligned 8-byte page; never cross a page boundary. */
  HAL_StatusTypeDef result = HAL_I2C_Mem_Write(test_i2c, EEPROM_ADDRESS, EEPROM_TEST_OFFSET,
                        I2C_MEMADD_SIZE_8BIT, record, sizeof(record),
                        I2C_TIMEOUT_MS);
  result = FinishTransfer(result, 0U);
  if (result != HAL_OK)
  {
    goto done;
  }

  /* ACK polling waits for the EEPROM's internal programming cycle. */
  uint32_t started = HAL_GetTick();
  save_failure = "EEPROM write ACK timeout";
  while ((result = EEPROMReady(1, 5)) != HAL_OK)
  {
    if ((uint32_t)(HAL_GetTick() - started) >= 20U)
    {
      result = HAL_TIMEOUT;
      goto done;
    }
  }

  save_failure = "EEPROM verify read failed";
  result = ReadRecord(readback);
  if (result != HAL_OK)
  {
    goto done;
  }
  for (uint32_t i = 0; i < sizeof(record); ++i)
  {
    if (record[i] != readback[i])
    {
      save_failure = "EEPROM verify mismatch";
      mismatch_offset = (uint8_t)(EEPROM_TEST_OFFSET + i);
      mismatch_expected = record[i];
      mismatch_actual = readback[i];
      result = HAL_ERROR;
      goto done;
    }
  }
  save_failure = NULL;
done:
  ResumeLCD(lcd_clock);
  return result;
}

static void ShowSaveFailure(HAL_StatusTypeDef result)
{
  if (save_failure != NULL && strcmp(save_failure, "EEPROM verify mismatch") == 0)
  {
    char text[30];
    ShowStatus(save_failure);
    (void)snprintf(text, sizeof(text), "Addr=%02X want=%02X got=%02X",
                   (unsigned)mismatch_offset, (unsigned)mismatch_expected,
                   (unsigned)mismatch_actual);
    LCD_DrawString(0, 180, text);
  }
  else
  {
    ShowI2CFailure(save_failure, result);
  }
}

void EEPROM_Test_Init(I2C_HandleTypeDef *i2c)
{
  uint8_t record[EEPROM_RECORD_SIZE];
  uint8_t blank = 1U;
  uint8_t valid = 1U;
  test_i2c = i2c;
  test_ready = 0U;

  LCD_DrawString(0, 20, "PA0 / KEY1: increment");
  LCD_DrawString(0, 140, "Power cycle after Saved");
  ShowStatus("Reading EEPROM...");
  HAL_StatusTypeDef result = EEPROMReady(3, 10);
  if (result != HAL_OK)
  {
    ShowI2CFailure("EEPROM ACK failed (0x50)", result);
    return;
  }
  result = ReadRecord(record);
  if (result != HAL_OK)
  {
    ShowI2CFailure("EEPROM memory read failed", result);
    return;
  }

  for (uint32_t i = 0; i < EEPROM_RECORD_SIZE; ++i)
  {
    if (record[i] != 0xFFU)
    {
      blank = 0U;
    }
  }
  for (uint32_t i = 0; i < 4U; ++i)
  {
    if ((uint8_t)(record[i] ^ record[i + 4U]) != 0xFFU)
    {
      valid = 0U;
    }
  }
  if (blank != 0U)
  {
    counter = 0;
    result = SaveCounter(counter);
    if (result != HAL_OK)
    {
      ShowSaveFailure(result);
      return;
    }
    ShowStatus("Initialized 0 / Saved");
  }
  else if (valid != 0U && DecodeValue(record) <= INT32_MAX)
  {
    counter = (int32_t)DecodeValue(record);
    ShowStatus("Loaded from EEPROM");
  }
  else
  {
    /* Preserve unexpected data rather than silently overwriting it. */
    ShowStatus("Invalid EEPROM test data");
    return;
  }

  ShowCounter();
  /* If held at boot, require release before counting the next press. */
  last_raw = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
  stable_button = last_raw;
  button_changed_at = HAL_GetTick();
  test_ready = 1U;
}

void EEPROM_Test_Poll(void)
{
  if (test_ready == 0U)
  {
    return;
  }

  GPIO_PinState raw = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
  uint32_t now = HAL_GetTick();
  if (raw != last_raw)
  {
    last_raw = raw;
    button_changed_at = now;
  }
  if (raw != stable_button
      && (uint32_t)(now - button_changed_at) >= BUTTON_DEBOUNCE_MS)
  {
    stable_button = raw;
    if (stable_button == GPIO_PIN_SET)
    {
      if (counter == INT32_MAX)
      {
        ShowStatus("Counter at maximum");
        return;
      }
      ShowStatus("Saving...");
      HAL_StatusTypeDef result = SaveCounter(counter + 1);
      if (result == HAL_OK)
      {
        ++counter;
        ShowCounter();
        ShowStatus("Saved / readback OK");
      }
      else
      {
        /* A failed write may be partial: stop and inspect after reboot. */
        test_ready = 0U;
        ShowSaveFailure(result);
      }
    }
  }
}
