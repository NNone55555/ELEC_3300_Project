#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Core/Src/pmw3360.c"
Debug_Type debug;
static DWT_Type dwt;
static uint32_t selected_at, address_at, previous_byte_at;
static int selected, streaming, downloaded, first_read, prime, bad_srom,
    fail_upload_at = -1, fail_burst, stopped_counter;
static uint8_t registers[128], address, packet[7];
DWT_Type *MockDWT(void)
{
  if (!stopped_counter)
    dwt.CYCCNT += 72;
  return &dwt;
}
void MockNop(void)
{
  if (!stopped_counter)
    dwt.CYCCNT += 72;
}
uint32_t HAL_RCC_GetHCLKFreq(void)
{
  return 72000000;
}
void osDelay(uint32_t ms)
{
  (void)ms;
  assert(!selected);
}
void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState value)
{
  assert(port == PMW_NCS_GPIO_Port && pin == PMW_NCS_Pin);
  if (value == GPIO_PIN_RESET)
  {
    assert(!selected);
    selected = 1;
    selected_at = dwt.CYCCNT;
    address = 0xff;
  }
  else
  {
    if (streaming)
    {
      assert(dwt.CYCCNT - previous_byte_at >= 15 * 72);
      if (downloaded == 4094)
      {
        registers[0x2a] = bad_srom ? 0 : 4;
        first_read = 1;
      }
      streaming = 0;
    }
    selected = 0;
  }
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *data, uint16_t size,
                                   uint32_t timeout)
{
  (void)h;
  (void)timeout;
  assert(selected);
  if (streaming)
  {
    assert(size == 1 && dwt.CYCCNT - previous_byte_at >= 15 * 72);
    previous_byte_at = dwt.CYCCNT;
    assert(*data == pmw3360_srom[downloaded]);
    if (downloaded == fail_upload_at)
      return HAL_TIMEOUT;
    ++downloaded;
    return HAL_OK;
  }
  assert(dwt.CYCCNT - selected_at >= 72);
  if (first_read)
  {
    assert(size == 1 && *data == 0x2a);
    first_read = 0;
  }
  if (size == 2)
  {
    assert(data[0] & 0x80);
    uint8_t a = data[0] & 0x7f;
    registers[a] = data[1];
    if (a == 0x3a)
    {
      assert(data[1] == 0x5a);
      registers[0x2a] = 0;
      prime = 0;
    }
    if (a == 0x50)
      prime = 1;
    return HAL_OK;
  }
  assert(size == 1);
  address = *data;
  address_at = dwt.CYCCNT;
  if (address == 0xe2)
  {
    streaming = 1;
    downloaded = 0;
    previous_byte_at = dwt.CYCCNT;
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx,
                                          uint16_t size, uint32_t timeout)
{
  (void)h;
  (void)timeout;
  assert(selected && !streaming);
  for (unsigned i = 0; i < size; i++)
    assert(tx[i] == 0);
  if (address == 0x50)
  {
    assert(prime && size == 7 && dwt.CYCCNT - address_at >= 35 * 72);
    if (fail_burst)
      return HAL_TIMEOUT;
    memcpy(rx, packet, 7);
  }
  else
  {
    assert(size == 1 && address < 128 && dwt.CYCCNT - address_at >= 160 * 72);
    *rx = registers[address];
    prime = 0;
  }
  return HAL_OK;
}
static void reset_mock(void)
{
  memset(registers, 0, sizeof registers);
  registers[0] = 0x42;
  registers[0x3f] = 0xbd;
  bad_srom = fail_burst = stopped_counter = 0;
  fail_upload_at = -1;
  selected = streaming = first_read = prime = 0;
}
int main(void)
{
  SPI_HandleTypeDef spi = {0};
  Pmw3360Motion motion;
  reset_mock();
  assert(Pmw3360_Init(&spi, 800) == PMW3360_OK);
  assert(downloaded == 4094 && registers[0xf] == 7 && prime && !selected);
  assert(Pmw3360_SetCpi(2400) == PMW3360_OK && registers[0xf] == 23 && prime && downloaded == 4094);
  assert(Pmw3360_SetCpi(0) == PMW3360_CONFIG_ERROR && Pmw3360_SetCpi(250) == PMW3360_CONFIG_ERROR &&
         Pmw3360_SetCpi(12100) == PMW3360_CONFIG_ERROR);
  assert(Pmw3360_SetCpi(12000) == PMW3360_OK && registers[0xf] == 119 && prime &&
         downloaded == 4094);
  uint8_t moving[] = {0xa0, 0x40, 0x34, 0x12, 0xfe, 0xff, 50};
  memcpy(packet, moving, 7);
  assert(Pmw3360_ReadMotion(&motion) == PMW3360_OK && motion.dx == 4660 && motion.dy == -2 &&
         motion.quality == 50 && !motion.lifted);
  packet[0] = 0xa8;
  assert(Pmw3360_ReadMotion(&motion) == PMW3360_OK && motion.dx == 0 && motion.lifted);
  packet[0] = 0x20;
  assert(Pmw3360_ReadMotion(&motion) == PMW3360_OK && motion.dx == 0 && motion.dy == 0);
  fail_burst = 1;
  assert(Pmw3360_ReadMotion(&motion) == PMW3360_SPI_ERROR && !selected);
  reset_mock();
  fail_upload_at = 123;
  assert(Pmw3360_Init(&spi, 800) == PMW3360_SPI_ERROR && !selected && downloaded == 123);
  reset_mock();
  bad_srom = 1;
  assert(Pmw3360_Init(&spi, 800) == PMW3360_SROM_ERROR);
  reset_mock();
  registers[0] = 0xff;
  assert(Pmw3360_Init(&spi, 800) == PMW3360_ID_ERROR);
  reset_mock();
  stopped_counter = 1;
  assert(Pmw3360_Init(&spi, 800) == PMW3360_TIMER_ERROR);
  reset_mock();
  assert(Pmw3360_Init(&spi, 801) == PMW3360_CONFIG_ERROR);
  assert(SignedDelta(0, 0x80) == -32768 && SignedDelta(0xff, 0x7f) == 32767);
  puts("PASS: optical ID, exact SROM content/upload/CS/timing, runtime CPI/readback/burst re-arm "
       "without SROM reload, signed motion/lift filtering, error cleanup.");
  return 0;
}
