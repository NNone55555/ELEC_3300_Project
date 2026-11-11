#include "pmw3360_test.h"
#include "pmw3360_srom.h"
#include "main.h"
#include "lcd.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define REG_PRODUCT_ID          0x00U
#define REG_REVISION_ID         0x01U
#define REG_CONFIG1             0x0FU
#define REG_CONFIG2             0x10U
#define REG_SROM_ENABLE         0x13U
#define REG_SROM_ID             0x2AU
#define REG_POWER_UP_RESET      0x3AU
#define REG_INVERSE_PRODUCT_ID  0x3FU
#define REG_MOTION_BURST        0x50U
#define REG_SROM_LOAD_BURST     0x62U
#define TEST_CPI                800U

static SPI_HandleTypeDef *test_spi;
static UART_HandleTypeDef *test_uart;
static uint32_t cycles_per_us, last_display;
static uint8_t ready;
static int32_t total_x, total_y, interval_x, interval_y;

static void ShowLine(uint16_t y, const char *text)
{
  LCD_Clear(0, y, 240, HEIGHT_EN_CHAR, BACKGROUND);
  LCD_DrawString(0, y, text);
}

static void PrintSerial(const char *text)
{
  if (HAL_UART_Transmit(test_uart, (const uint8_t *)text,
                        (uint16_t)strlen(text), 100) != HAL_OK)
  {
    ShowLine(220, "USART1 TX error");
  }
}

static void Fail(const char *message)
{
  ready = 0U;
  HAL_GPIO_WritePin(PMW_NCS_GPIO_Port, PMW_NCS_Pin, GPIO_PIN_SET);
  ShowLine(140, message);
  PrintSerial(message);
  PrintSerial("\r\nRestart after checking sensor\r\n");
}

static void DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = us * cycles_per_us;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles) { __NOP(); }
}

static void SelectSensor(void)
{
  HAL_GPIO_WritePin(PMW_NCS_GPIO_Port, PMW_NCS_Pin, GPIO_PIN_RESET);
  DelayUs(1);
}

static void DeselectSensor(void)
{
  HAL_GPIO_WritePin(PMW_NCS_GPIO_Port, PMW_NCS_Pin, GPIO_PIN_SET);
  DelayUs(1);
}

static HAL_StatusTypeDef ReadRegister(uint8_t address, uint8_t *value)
{
  uint8_t dummy = 0U;
  address &= 0x7FU;
  SelectSensor();
  HAL_StatusTypeDef result = HAL_SPI_Transmit(test_spi, &address, 1, 20);
  if (result == HAL_OK)
  {
    DelayUs(160);
    result = HAL_SPI_TransmitReceive(test_spi, &dummy, value, 1, 20);
  }
  DelayUs(1);
  DeselectSensor();
  DelayUs(20);
  return result;
}

static HAL_StatusTypeDef WriteRegister(uint8_t address, uint8_t value)
{
  uint8_t data[2] = {address | 0x80U, value};
  SelectSensor();
  HAL_StatusTypeDef result = HAL_SPI_Transmit(test_spi, data, sizeof(data), 20);
  DelayUs(35);
  DeselectSensor();
  DelayUs(180); /* Conservative write-to-next-command spacing. */
  return result;
}

static HAL_StatusTypeDef UploadSROM(uint8_t *srom_id)
{
  if (WriteRegister(REG_CONFIG2, 0x00U) != HAL_OK
      || WriteRegister(REG_SROM_ENABLE, 0x1DU) != HAL_OK) { return HAL_ERROR; }
  HAL_Delay(10);
  if (WriteRegister(REG_SROM_ENABLE, 0x18U) != HAL_OK) { return HAL_ERROR; }

  uint8_t command = REG_SROM_LOAD_BURST | 0x80U;
  SelectSensor();
  HAL_StatusTypeDef result = HAL_SPI_Transmit(test_spi, &command, 1, 20);
  DelayUs(15);
  /* Keep CS asserted for the whole image, with >=15 us between bytes. */
  for (uint32_t i = 0; result == HAL_OK && i < sizeof(pmw3360_srom); ++i)
  {
    uint8_t byte = pmw3360_srom[i];
    result = HAL_SPI_Transmit(test_spi, &byte, 1, 20);
    DelayUs(15);
  }
  DeselectSensor();
  DelayUs(200);
  if (result != HAL_OK) { return result; }
  /* SROM_ID must be the first register accessed after the download. */
  return ReadRegister(REG_SROM_ID, srom_id);
}

void PMW3360_Test_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *uart)
{
  test_spi = spi;
  test_uart = uart;
  ready = 0U;
  total_x = total_y = interval_x = interval_y = 0;
  HAL_GPIO_WritePin(PMW_NCS_GPIO_Port, PMW_NCS_Pin, GPIO_PIN_SET);
  ShowLine(20, "SPI2 mode 3 / 800 CPI");
  ShowLine(40, "Initializing sensor...");

  /* Cortex-M3 cycle counter supplies microsecond timing without another
   * timer/DMA channel. Fail instead of hanging if it is unavailable.
   */
  cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  uint32_t before = DWT->CYCCNT;
  for (volatile uint32_t i = 0; i < 32U; ++i) { __NOP(); }
  if (cycles_per_us == 0U || DWT->CYCCNT == before)
  {
    Fail("Microsecond timer error");
    return;
  }

  HAL_Delay(50);
  uint8_t product = 0U, inverse = 0U, revision = 0U;
  if (ReadRegister(REG_PRODUCT_ID, &product) != HAL_OK
      || ReadRegister(REG_INVERSE_PRODUCT_ID, &inverse) != HAL_OK
      || ReadRegister(REG_REVISION_ID, &revision) != HAL_OK)
  {
    Fail("SPI ID read error");
    return;
  }
  char text[80];
  (void)snprintf(text, sizeof(text), "ID=%02X INV=%02X REV=%02X",
                 (unsigned)product, (unsigned)inverse, (unsigned)revision);
  ShowLine(60, text);
  PrintSerial("\r\nPMW3360 motion test: ");
  PrintSerial(text);
  PrintSerial("\r\n");
  if (product != 0x42U || inverse != 0xBDU)
  {
    Fail("Sensor ID mismatch");
    return;
  }

  /* Reset via SPI; no dependence on an external RESET connection. */
  if (WriteRegister(REG_POWER_UP_RESET, 0x5AU) != HAL_OK)
  {
    Fail("Sensor reset write error");
    return;
  }
  HAL_Delay(50);
  for (uint8_t address = 0x02U; address <= 0x06U; ++address)
  {
    uint8_t discarded;
    if (ReadRegister(address, &discarded) != HAL_OK)
    {
      Fail("Reset motion clear error");
      return;
    }
  }

  ShowLine(100, "Loading SROM 0x04...");
  uint8_t srom_id = 0U;
  if (UploadSROM(&srom_id) != HAL_OK)
  {
    Fail("SROM SPI upload error");
    return;
  }
  (void)snprintf(text, sizeof(text), "SROM=%02X (expected 04)", (unsigned)srom_id);
  ShowLine(100, text);
  PrintSerial(text);
  PrintSerial("\r\n");
  if (srom_id != PMW3360_SROM_ID)
  {
    Fail("SROM verification failed");
    return;
  }

  /* Keep REST disabled for this bench test; use equal X/Y resolution. */
  uint8_t resolution = 0U;
  if (WriteRegister(REG_CONFIG2, 0x00U) != HAL_OK
      || WriteRegister(REG_CONFIG1, TEST_CPI / 100U - 1U) != HAL_OK
      || ReadRegister(REG_CONFIG1, &resolution) != HAL_OK
      || resolution != TEST_CPI / 100U - 1U
      || WriteRegister(REG_MOTION_BURST, 0U) != HAL_OK)
  {
    Fail("Sensor config verify error");
    return;
  }
  HAL_Delay(10);
  ShowLine(40, "Move sensor on mousepad");
  ShowLine(140, "Tracking ready / 800 CPI");
  PrintSerial("Tracking ready / 800 CPI; X/Y are counts\r\n");
  last_display = HAL_GetTick();
  ready = 1U;
}

static int32_t AddCounts(int32_t current, int32_t delta)
{
  int64_t sum = (int64_t)current + delta;
  if (sum > INT32_MAX) { return INT32_MAX; }
  if (sum < INT32_MIN) { return INT32_MIN; }
  return (int32_t)sum;
}

static int32_t SignedDelta(uint8_t low, uint8_t high)
{
  uint32_t raw = (uint32_t)low | ((uint32_t)high << 8);
  return raw >= 0x8000U ? (int32_t)raw - 65536 : (int32_t)raw;
}

void PMW3360_Test_Poll(void)
{
  if (ready == 0U) { return; }

  uint8_t command = REG_MOTION_BURST;
  uint8_t dummy[7] = {0};
  uint8_t data[7]; /* Motion, observation, dXL, dXH, dYL, dYH, SQUAL. */
  SelectSensor();
  HAL_StatusTypeDef result = HAL_SPI_Transmit(test_spi, &command, 1, 20);
  if (result == HAL_OK)
  {
    DelayUs(35);
    result = HAL_SPI_TransmitReceive(test_spi, dummy, data, sizeof(data), 20);
  }
  DelayUs(1);
  DeselectSensor();
  if (result != HAL_OK)
  {
    Fail("Motion burst SPI error");
    return;
  }

  uint8_t motion = (data[0] & 0x80U) != 0U;
  uint8_t lifted = (data[0] & 0x08U) != 0U;
  if (motion != 0U && lifted == 0U)
  {
    int32_t dx = SignedDelta(data[2], data[3]);
    int32_t dy = SignedDelta(data[4], data[5]);
    interval_x = AddCounts(interval_x, dx);
    interval_y = AddCounts(interval_y, dy);
    total_x = AddCounts(total_x, dx);
    total_y = AddCounts(total_y, dy);
  }

  uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - last_display) < 100U) { return; }
  last_display = now;
  char text[100];
  (void)snprintf(text, sizeof(text), "dX:%ld", (long)interval_x);
  ShowLine(120, text);
  (void)snprintf(text, sizeof(text), "dY:%ld", (long)interval_y);
  ShowLine(140, text);
  (void)snprintf(text, sizeof(text), "X total:%ld", (long)total_x);
  ShowLine(160, text);
  (void)snprintf(text, sizeof(text), "Y total:%ld", (long)total_y);
  ShowLine(180, text);
  (void)snprintf(text, sizeof(text), "SQUAL=%u LIFT=%u MOTpin=%u", (unsigned)data[6],
                 (unsigned)lifted,
                 (unsigned)HAL_GPIO_ReadPin(PMW_MOTION_GPIO_Port, PMW_MOTION_Pin));
  ShowLine(200, text);
  (void)snprintf(text, sizeof(text), "dX=%ld dY=%ld X=%ld Y=%ld SQ=%u LIFT=%u\r\n",
                 (long)interval_x, (long)interval_y, (long)total_x, (long)total_y,
                 (unsigned)data[6], (unsigned)lifted);
  PrintSerial(text);
  interval_x = interval_y = 0;
}
