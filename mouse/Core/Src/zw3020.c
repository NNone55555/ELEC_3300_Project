/* EF-01 framing and commands adapted from Gavin's HLK-ZW library (MIT).
 * See third_party/hlk_fingerprint/LICENSE and README.md.
 */
#include "zw3020.h"
#include "cmsis_os2.h"
#include <string.h>

#define COMMAND_TIMEOUT_MS 1000U
#define MODULE_ADDRESS 0xFFFFFFFFUL
#define MODULE_PASSWORD 0x00000000UL
#define RX_BUFFER_SIZE 256U

static UART_HandleTypeDef *sensor_uart;
static uint8_t received_byte;
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile bool rx_lost;
static uint8_t response[80];
static uint8_t last_command;

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static bool PrepareReceiver(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  rx_tail = rx_head;
  rx_lost = false;
  if (sensor_uart->RxState == HAL_UART_STATE_READY)
  {
    __HAL_UART_CLEAR_OREFLAG(sensor_uart);
    if (HAL_UART_Receive_IT(sensor_uart, &received_byte, 1) != HAL_OK)
    {
      rx_lost = true;
    }
  }
  bool ready = sensor_uart->RxState == HAL_UART_STATE_BUSY_RX && !rx_lost;
  __set_PRIMASK(interrupt_state);
  return ready;
}

bool Zw3020_Init(UART_HandleTypeDef *uart)
{
  sensor_uart = uart;
  rx_head = 0;
  rx_tail = 0;
  rx_lost = false;
  return PrepareReceiver();
}

void Zw3020_OnRxComplete(UART_HandleTypeDef *uart)
{
  if (uart != sensor_uart)
  {
    return;
  }
  uint16_t next = (rx_head + 1U) % RX_BUFFER_SIZE;
  if (next == rx_tail)
  {
    rx_lost = true;
  }
  else
  {
    rx_buffer[rx_head] = received_byte;
    __DMB();
    rx_head = next;
  }
  if (HAL_UART_Receive_IT(uart, &received_byte, 1) != HAL_OK)
  {
    rx_lost = true;
  }
}

void Zw3020_OnUartError(UART_HandleTypeDef *uart)
{
  if (uart != sensor_uart)
  {
    return;
  }
  rx_lost = true;
  /* HAL ends RX on overrun. Recover reception, but reject that transaction. */
  if (uart->RxState == HAL_UART_STATE_READY)
  {
    (void)HAL_UART_Receive_IT(uart, &received_byte, 1);
  }
}

static int16_t ReceiveBytes(uint8_t *data, uint16_t size, uint32_t started)
{
  uint16_t received = 0;
  while (received < size)
  {
    if (rx_lost)
    {
      return ZW3020_UART_ERROR;
    }
    if ((uint32_t)(HAL_GetTick() - started) >= COMMAND_TIMEOUT_MS)
    {
      return ZW3020_TIMEOUT;
    }
    uint16_t tail = rx_tail;
    if (tail != rx_head)
    {
      __DMB();
      data[received++] = rx_buffer[tail];
      rx_tail = (tail + 1U) % RX_BUFFER_SIZE;
    }
    else
    {
      /* ISR continues receiving while the task sleeps or motion is sampled. */
      osDelay(1);
    }
  }
  return ZW3020_OK;
}

/* Return the sensor confirm code, or a negative transport/protocol error.
 * Successful response data begins at response[10]. Never copy an untrusted
 * response length into a caller buffer. The fingerprint task owns UART2 commands.
 */
static int16_t Command(uint8_t instruction, const uint8_t *parameters,
                       uint8_t parameter_count, uint8_t minimum_data)
{
  uint8_t tx[17] = {0xEF, 0x01, (uint8_t)(MODULE_ADDRESS >> 24),
                    (uint8_t)(MODULE_ADDRESS >> 16), (uint8_t)(MODULE_ADDRESS >> 8),
                    (uint8_t)MODULE_ADDRESS, 0x01, 0, 0};
  last_command = instruction;
  if (parameter_count > 5U)
  {
    return ZW3020_BAD_PACKET;
  }
  tx[8] = parameter_count + 3U;
  tx[9] = instruction;
  if (parameter_count != 0U)
  {
    memcpy(&tx[10], parameters, parameter_count);
  }
  uint16_t sum = 0;
  for (uint8_t i = 6U; i < 10U + parameter_count; ++i)
  {
    sum += tx[i];
  }
  tx[10U + parameter_count] = (uint8_t)(sum >> 8);
  tx[11U + parameter_count] = (uint8_t)sum;

  if (!PrepareReceiver())
  {
    return ZW3020_UART_ERROR;
  }
  if (HAL_UART_Transmit(sensor_uart, tx, parameter_count + 12U, 100) != HAL_OK)
  {
    return ZW3020_UART_ERROR;
  }

  uint32_t started = HAL_GetTick();
  uint8_t matched = 0U;
  while (matched < 2U)
  {
    uint8_t byte;
    int16_t result = ReceiveBytes(&byte, 1, started);
    if (result != 0)
    {
      return result;
    }
    if (matched == 1U && byte == 0x01U)
    {
      matched = 2U;
    }
    else
    {
      matched = byte == 0xEFU ? 1U : 0U;
    }
  }
  response[0] = 0xEF;
  response[1] = 0x01;
  int16_t result = ReceiveBytes(&response[2], 7, started);
  if (result != 0)
  {
    return result;
  }
  uint16_t length = ReadU16(&response[7]);
  if (memcmp(&response[2], &tx[2], 4) != 0 || response[6] != 0x07U
      || length < 3U || length > sizeof(response) - 9U)
  {
    return ZW3020_BAD_PACKET;
  }
  result = ReceiveBytes(&response[9], length, started);
  if (result != 0)
  {
    return result;
  }
  sum = 0;
  for (uint16_t i = 6U; i < 9U + length - 2U; ++i)
  {
    sum += response[i];
  }
  if (sum != ReadU16(&response[9U + length - 2U])
      || (response[9] == 0U && length - 3U < minimum_data))
  {
    return ZW3020_BAD_PACKET;
  }
  return response[9];
}

int16_t Zw3020_Connect(Zw3020Info *info)
{
  osDelay(300);
  const uint8_t password[4] = {(uint8_t)(MODULE_PASSWORD >> 24),
                               (uint8_t)(MODULE_PASSWORD >> 16),
                               (uint8_t)(MODULE_PASSWORD >> 8),
                               (uint8_t)MODULE_PASSWORD};
  int16_t result = Command(0x13, password, 4, 0);
  if (result != ZW3020_OK)
  {
    return result;
  }
  result = Command(0x0F, NULL, 0, 16);
  if (result != ZW3020_OK)
  {
    return result;
  }
  info->capacity = ReadU16(&response[14]);
  if (info->capacity == 0U)
  {
    return ZW3020_BAD_PACKET;
  }
  result = Command(0x1D, NULL, 0, 2);
  if (result == ZW3020_OK)
  {
    info->count = ReadU16(&response[10]);
    if (info->count > info->capacity)
    {
      return ZW3020_BAD_PACKET;
    }
  }
  return result;
}

int16_t Zw3020_GetImage(void)
{
  return Command(0x01, NULL, 0, 0);
}

int16_t Zw3020_Extract(uint8_t buffer)
{
  return Command(0x02, &buffer, 1, 0);
}

int16_t Zw3020_Combine(void)
{
  return Command(0x05, NULL, 0, 0);
}

int16_t Zw3020_Store(uint16_t id)
{
  uint8_t parameters[3] = {1, (uint8_t)(id >> 8), (uint8_t)id};
  return Command(0x06, parameters, sizeof(parameters), 0);
}

int16_t Zw3020_Search(uint16_t capacity, Zw3020Match *match)
{
  /* ZW3020 uses standard Search 0x04. */
  uint8_t parameters[5] = {1, 0, 0, (uint8_t)(capacity >> 8), (uint8_t)capacity};
  int16_t result = Command(0x04, parameters, sizeof(parameters), 4);
  if (result == ZW3020_OK)
  {
    match->id = ReadU16(&response[10]);
    match->score = ReadU16(&response[12]);
    if (match->id >= capacity)
    {
      return ZW3020_BAD_PACKET;
    }
  }
  return result;
}

int16_t Zw3020_FindFreeSlot(uint16_t capacity, uint16_t *id)
{
  for (uint16_t page = 0U; page * 256UL < capacity; ++page)
  {
    uint8_t parameter = (uint8_t)page;
    uint16_t slots = capacity - page * 256UL;
    if (slots > 256U)
    {
      slots = 256U;
    }
    int16_t result = Command(0x1F, &parameter, 1, (uint8_t)((slots + 7U) / 8U));
    if (result != ZW3020_OK)
    {
      return result;
    }
    for (uint16_t bit = 0U; bit < slots; ++bit)
    {
      if ((response[10U + bit / 8U] & (1U << (bit % 8U))) == 0U)
      {
        *id = page * 256U + bit;
        return ZW3020_OK;
      }
    }
  }
  return ZW3020_FULL;
}

uint8_t Zw3020_LastCommand(void)
{
  return last_command;
}

int16_t Zw3020_Delete(uint16_t id)
{
  const uint8_t parameters[4] = {(uint8_t)(id >> 8), (uint8_t)id, 0, 1};
  return Command(0x0C, parameters, sizeof(parameters), 0);
}

int16_t Zw3020_IsOccupied(uint16_t id, bool *occupied)
{
  uint8_t page = id / 256U;
  int16_t result = Command(0x1F, &page, 1, 32);
  if (result == ZW3020_OK) *occupied = (response[10U + (id % 256U) / 8U] & (1U << (id % 8U))) != 0U;
  return result;
}
