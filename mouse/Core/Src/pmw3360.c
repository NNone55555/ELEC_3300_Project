#include "pmw3360.h"
#include "pmw3360_srom.h"
#include "main.h"
#include "cmsis_os2.h"

#define REG_PRODUCT_ID          0x00U
#define REG_CONFIG1             0x0FU
#define REG_CONFIG2             0x10U
#define REG_SROM_ENABLE         0x13U
#define REG_SROM_ID             0x2AU
#define REG_POWER_UP_RESET      0x3AU
#define REG_INVERSE_PRODUCT_ID  0x3FU
#define REG_MOTION_BURST        0x50U
#define REG_SROM_LOAD_BURST     0x62U

static SPI_HandleTypeDef *sensor_spi;
static uint32_t cycles_per_us;

static void DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = us * cycles_per_us;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles)
  {
    __NOP();
  }
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
  HAL_StatusTypeDef result = HAL_SPI_Transmit(sensor_spi, &address, 1, 20);
  if (result == HAL_OK)
  {
    DelayUs(160);
    result = HAL_SPI_TransmitReceive(sensor_spi, &dummy, value, 1, 20);
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
  HAL_StatusTypeDef result = HAL_SPI_Transmit(sensor_spi, data, sizeof(data), 20);
  DelayUs(35);
  DeselectSensor();
  DelayUs(180); /* Conservative write-to-next-command spacing. */
  return result;
}

static HAL_StatusTypeDef UploadSROM(uint8_t *srom_id)
{
  if (WriteRegister(REG_CONFIG2, 0x00U) != HAL_OK
      || WriteRegister(REG_SROM_ENABLE, 0x1DU) != HAL_OK)
  {
    return HAL_ERROR;
  }
  osDelay(10);
  if (WriteRegister(REG_SROM_ENABLE, 0x18U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  uint8_t command = REG_SROM_LOAD_BURST | 0x80U;
  SelectSensor();
  HAL_StatusTypeDef result = HAL_SPI_Transmit(sensor_spi, &command, 1, 20);
  DelayUs(15);
  /* Keep CS asserted for the whole image, with >=15 us between bytes. */
  for (uint32_t i = 0; result == HAL_OK && i < sizeof(pmw3360_srom); ++i)
  {
    uint8_t byte = pmw3360_srom[i];
    result = HAL_SPI_Transmit(sensor_spi, &byte, 1, 20);
    DelayUs(15);
  }
  DeselectSensor();
  DelayUs(200);
  if (result != HAL_OK)
  {
    return result;
  }
  /* SROM_ID must be the first register accessed after the download. */
  return ReadRegister(REG_SROM_ID, srom_id);
}

Pmw3360Status Pmw3360_Init(SPI_HandleTypeDef *spi, uint16_t cpi)
{
  sensor_spi = spi;
  HAL_GPIO_WritePin(PMW_NCS_GPIO_Port, PMW_NCS_Pin, GPIO_PIN_SET);
  if (cpi < 100U || cpi > 12000U || cpi % 100U != 0U)
  {
    return PMW3360_CONFIG_ERROR;
  }
  cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  uint32_t before = DWT->CYCCNT;
  for (volatile uint32_t i = 0; i < 32U; ++i)
  {
    __NOP();
  }
  if (cycles_per_us == 0U || DWT->CYCCNT == before)
  {
    return PMW3360_TIMER_ERROR;
  }
  osDelay(50);
  uint8_t product, inverse;
  if (ReadRegister(REG_PRODUCT_ID, &product) != HAL_OK
      || ReadRegister(REG_INVERSE_PRODUCT_ID, &inverse) != HAL_OK)
  {
    return PMW3360_SPI_ERROR;
  }
  if (product != 0x42U || inverse != 0xBDU)
  {
    return PMW3360_ID_ERROR;
  }
  if (WriteRegister(REG_POWER_UP_RESET, 0x5AU) != HAL_OK)
  {
    return PMW3360_SPI_ERROR;
  }
  osDelay(50);
  for (uint8_t address = 0x02U; address <= 0x06U; ++address)
  {
    uint8_t discarded;
    if (ReadRegister(address, &discarded) != HAL_OK)
    {
      return PMW3360_SPI_ERROR;
    }
  }
  uint8_t srom_id;
  if (UploadSROM(&srom_id) != HAL_OK)
  {
    return PMW3360_SPI_ERROR;
  }
  if (srom_id != PMW3360_SROM_ID)
  {
    return PMW3360_SROM_ERROR;
  }
  uint8_t resolution;
  uint8_t expected_resolution = (uint8_t)(cpi / 100U - 1U);
  if (WriteRegister(REG_CONFIG2, 0x00U) != HAL_OK
      || WriteRegister(REG_CONFIG1, expected_resolution) != HAL_OK
      || ReadRegister(REG_CONFIG1, &resolution) != HAL_OK
      || resolution != expected_resolution
      || WriteRegister(REG_MOTION_BURST, 0U) != HAL_OK)
  {
    return PMW3360_CONFIG_ERROR;
  }
  osDelay(10);
  return PMW3360_OK;
}

static int16_t SignedDelta(uint8_t low, uint8_t high)
{
  uint32_t raw = (uint32_t)low | ((uint32_t)high << 8);
  int32_t value = (int32_t)raw;
  if (raw >= 0x8000U)
  {
    value -= 65536;
  }
  return (int16_t)value;
}

Pmw3360Status Pmw3360_SetCpi(uint16_t cpi)
{
  if (sensor_spi == NULL || cpi < 100U || cpi > 12000U || cpi % 100U != 0U)
    return PMW3360_CONFIG_ERROR;
  uint8_t expected = (uint8_t)(cpi / 100U - 1U), actual;
  if (WriteRegister(REG_CONFIG1, expected) != HAL_OK || ReadRegister(REG_CONFIG1, &actual) != HAL_OK
      || actual != expected || WriteRegister(REG_MOTION_BURST, 0U) != HAL_OK)
    return PMW3360_CONFIG_ERROR;
  return PMW3360_OK;
}

Pmw3360Status Pmw3360_ReadMotion(Pmw3360Motion *motion)
{
  uint8_t command = REG_MOTION_BURST;
  uint8_t dummy[7] = {0};
  uint8_t data[7];
  SelectSensor();
  HAL_StatusTypeDef result = HAL_SPI_Transmit(sensor_spi, &command, 1, 20);
  if (result == HAL_OK)
  {
    DelayUs(35);
    result = HAL_SPI_TransmitReceive(sensor_spi, dummy, data, sizeof(data), 20);
  }
  DelayUs(1);
  DeselectSensor();
  if (result != HAL_OK)
  {
    return PMW3360_SPI_ERROR;
  }
  motion->quality = data[6];
  motion->lifted = (data[0] & 0x08U) != 0U;
  motion->dx = 0;
  motion->dy = 0;
  if ((data[0] & 0x80U) != 0U && !motion->lifted)
  {
    motion->dx = SignedDelta(data[2], data[3]);
    motion->dy = SignedDelta(data[4], data[5]);
  }
  return PMW3360_OK;
}
