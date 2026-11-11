/* STM32 HAL adaptation of the EF-01 commands and enrollment flow in
 * GavinnnTann/HLK-ZW-Fingerprint-Sensor (MIT, copyright 2026 Gavin).
 * Source and license are in third_party/hlk_fingerprint/.
 */
#include "fingerprint_test.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

#define FP_TIMEOUT_MS 1000U
#define FP_SCAN_INTERVAL_MS 100U
#define FP_ENROLL_TIMEOUT_MS 15000U
#define FP_PASSWORD 0x00000000UL
#define FP_ADDRESS 0xFFFFFFFFUL
#define FP_TIMEOUT (-1)
#define FP_BAD_PACKET (-2)
#define FP_UART_ERROR (-3)
#define FP_FULL (-4)

typedef enum {
  SCAN_TO_MATCH,
  ENROLL_FIRST,
  ENROLL_LIFT,
  ENROLL_SECOND,
  WAIT_FOR_REMOVAL,
  SENSOR_FAULT
} TestState;

static UART_HandleTypeDef *sensor_uart;
static UART_HandleTypeDef *console_uart;
static uint8_t response[80];
static uint16_t capacity;
static uint16_t template_count;
static uint16_t enrollment_id;
static uint8_t last_command;
static TestState state;
static uint32_t stage_started;
static uint32_t last_scan;
static GPIO_PinState button_raw;
static GPIO_PinState button_stable;
static uint32_t button_changed;

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void ShowLine(uint16_t y, const char *text)
{
  LCD_Clear(0, y, 240, HEIGHT_EN_CHAR, BACKGROUND);
  LCD_DrawString(0, y, text);
}

static void Message(const char *text)
{
  ShowLine(80, text);
  if (HAL_UART_Transmit(console_uart, (const uint8_t *)text,
                        (uint16_t)strlen(text), 100) != HAL_OK)
  {
    ShowLine(200, "USART1 TX error");
  }
  (void)HAL_UART_Transmit(console_uart, (const uint8_t *)"\r\n", 2, 100);
}

static void SetState(TestState next, const char *message)
{
  state = next;
  stage_started = HAL_GetTick();
  Message(message);
}

static int16_t ReceiveBytes(uint8_t *data, uint16_t size, uint32_t started)
{
  uint32_t elapsed = (uint32_t)(HAL_GetTick() - started);
  if (elapsed >= FP_TIMEOUT_MS)
  {
    return FP_TIMEOUT;
  }
  HAL_StatusTypeDef result = HAL_UART_Receive(sensor_uart, data, size,
                                              FP_TIMEOUT_MS - elapsed);
  if (result == HAL_TIMEOUT)
  {
    return FP_TIMEOUT;
  }
  return result == HAL_OK ? 0 : FP_UART_ERROR;
}

/* Return the sensor confirm code, or a negative transport/protocol error.
 * Successful response data begins at response[10]. Never copy an untrusted
 * response length into a caller buffer. UART2 has one owner in this test.
 */
static int16_t Command(uint8_t instruction, const uint8_t *parameters,
                       uint8_t parameter_count, uint8_t minimum_data)
{
  uint8_t tx[17] = {0xEF, 0x01, (uint8_t)(FP_ADDRESS >> 24),
                    (uint8_t)(FP_ADDRESS >> 16), (uint8_t)(FP_ADDRESS >> 8),
                    (uint8_t)FP_ADDRESS, 0x01, 0, 0};
  last_command = instruction;
  if (parameter_count > 5U)
  {
    return FP_BAD_PACKET;
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

  /* Clear stale bytes/overrun from a previous timeout or module startup. */
  __HAL_UART_CLEAR_OREFLAG(sensor_uart);
  uint8_t discarded;
  for (uint8_t i = 0; i < sizeof(response); ++i)
  {
    if (HAL_UART_Receive(sensor_uart, &discarded, 1, 0) != HAL_OK)
    {
      break;
    }
  }
  if (HAL_UART_Transmit(sensor_uart, tx, parameter_count + 12U, 100) != HAL_OK)
  {
    return FP_UART_ERROR;
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
    return FP_BAD_PACKET;
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
    return FP_BAD_PACKET;
  }
  return response[9];
}

static void Failure(const char *operation, int16_t result)
{
  char text[30];
  Message(operation);
  if (result >= 0)
  {
    (void)snprintf(text, sizeof(text), "CMD=%02X confirm=%02X",
                   (unsigned)last_command, (unsigned)result);
  }
  else
  {
    const char *reason = "UART error";
    if (result == FP_TIMEOUT) { reason = "timeout / no reply"; }
    if (result == FP_BAD_PACKET) { reason = "bad packet/checksum"; }
    if (result == FP_FULL) { reason = "storage full"; }
    (void)snprintf(text, sizeof(text), "CMD=%02X %s", (unsigned)last_command, reason);
  }
  ShowLine(100, text);
  (void)HAL_UART_Transmit(console_uart, (const uint8_t *)text,
                          (uint16_t)strlen(text), 100);
  (void)HAL_UART_Transmit(console_uart, (const uint8_t *)"\r\n", 2, 100);
  state = result < 0 ? SENSOR_FAULT : WAIT_FOR_REMOVAL;
}

static void ShowStorage(void)
{
  char text[30];
  (void)snprintf(text, sizeof(text), "Stored: %u / %u", (unsigned)template_count,
                 (unsigned)capacity);
  ShowLine(60, text);
}

static int16_t FindFreeSlot(void)
{
  for (uint16_t page = 0U; page * 256UL < capacity; ++page)
  {
    uint8_t parameter = (uint8_t)page;
    uint16_t slots = capacity - page * 256UL;
    if (slots > 256U) { slots = 256U; }
    int16_t result = Command(0x1F, &parameter, 1, (uint8_t)((slots + 7U) / 8U));
    if (result != 0) { return result; }
    for (uint16_t bit = 0U; bit < slots; ++bit)
    {
      if ((response[10U + bit / 8U] & (1U << (bit % 8U))) == 0U)
      {
        enrollment_id = page * 256U + bit;
        return 0;
      }
    }
  }
  return FP_FULL;
}

void Fingerprint_Test_Init(UART_HandleTypeDef *sensor, UART_HandleTypeDef *console)
{
  sensor_uart = sensor;
  console_uart = console;
  state = SENSOR_FAULT;
  ShowLine(20, "USART2 / 57600 baud");
  ShowLine(40, "PA0: enroll / retry");
  ShowLine(140, "Scan enrolled finger to match");
  ShowLine(100, "");
  Message("Connecting to ZW3020...");
  HAL_Delay(300);
  const uint8_t password[4] = {(uint8_t)(FP_PASSWORD >> 24),
                               (uint8_t)(FP_PASSWORD >> 16),
                               (uint8_t)(FP_PASSWORD >> 8), (uint8_t)FP_PASSWORD};
  int16_t result = Command(0x13, password, 4, 0);
  if (result != 0)
  {
    Failure("Password/connect failed", result);
    state = SENSOR_FAULT;
  }
  else if ((result = Command(0x0F, NULL, 0, 16)) != 0)
  {
    Failure("Read parameters failed", result);
    state = SENSOR_FAULT;
  }
  else
  {
    capacity = ReadU16(&response[14]);
    if (capacity == 0U)
    {
      Failure("Invalid sensor capacity", FP_BAD_PACKET);
    }
    else if ((result = Command(0x1D, NULL, 0, 2)) != 0)
    {
      Failure("Read template count failed", result);
      state = SENSOR_FAULT;
    }
    else
    {
      template_count = ReadU16(&response[10]);
      ShowStorage();
      SetState(SCAN_TO_MATCH, "Ready: place finger to match");
    }
  }
  button_raw = button_stable = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
  button_changed = last_scan = HAL_GetTick();
}

void Fingerprint_Test_Poll(void)
{
  uint32_t now = HAL_GetTick();
  GPIO_PinState raw = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
  if (raw != button_raw)
  {
    button_raw = raw;
    button_changed = now;
  }
  if (raw != button_stable && (uint32_t)(now - button_changed) >= 30U)
  {
    button_stable = raw;
    if (raw == GPIO_PIN_SET)
    {
      if (state == SENSOR_FAULT)
      {
        Fingerprint_Test_Init(sensor_uart, console_uart);
        return;
      }
      if (state == ENROLL_FIRST || state == ENROLL_LIFT || state == ENROLL_SECOND)
      {
        SetState(WAIT_FOR_REMOVAL, "Enrollment cancelled");
        return;
      }
      int16_t result = FindFreeSlot();
      if (result != 0)
      {
        Failure("Cannot select free slot", result);
        return;
      }
      char text[30];
      (void)snprintf(text, sizeof(text), "Enrolling new ID %u", (unsigned)enrollment_id);
      ShowLine(100, text);
      SetState(ENROLL_FIRST, "Place finger: scan 1 of 2");
    }
  }
  now = HAL_GetTick(); /* Commands above can advance time and start a new stage. */
  if (state == SENSOR_FAULT || (uint32_t)(now - last_scan) < FP_SCAN_INTERVAL_MS)
  {
    return;
  }
  last_scan = now;
  if ((state == ENROLL_FIRST || state == ENROLL_LIFT || state == ENROLL_SECOND)
      && (uint32_t)(now - stage_started) >= FP_ENROLL_TIMEOUT_MS)
  {
    SetState(WAIT_FOR_REMOVAL, "Enrollment timed out");
    return;
  }

  int16_t result = Command(0x01, NULL, 0, 0);
  if (result == 0x02) /* No finger is a normal polling result. */
  {
    if (state == ENROLL_LIFT)
    {
      SetState(ENROLL_SECOND, "Same finger: scan 2 of 2");
    }
    else if (state == WAIT_FOR_REMOVAL)
    {
      SetState(SCAN_TO_MATCH, "Ready: place finger to match");
    }
    return;
  }
  if (result != 0)
  {
    Failure("Capture image failed", result);
    return;
  }
  if (state == ENROLL_LIFT || state == WAIT_FOR_REMOVAL) { return; }

  uint8_t buffer = state == ENROLL_SECOND ? 2U : 1U;
  result = Command(0x02, &buffer, 1, 0);
  if (result != 0)
  {
    Failure("Extract features failed", result);
    return;
  }
  if (state == ENROLL_FIRST)
  {
    SetState(ENROLL_LIFT, "Scan 1 OK: lift finger");
    return;
  }

  char text[30];
  if (state == ENROLL_SECOND)
  {
    result = Command(0x05, NULL, 0, 0);
    if (result != 0)
    {
      Failure("Combine scans failed", result);
      return;
    }
    uint8_t store[3] = {1, (uint8_t)(enrollment_id >> 8), (uint8_t)enrollment_id};
    result = Command(0x06, store, 3, 0);
    if (result != 0)
    {
      Failure("Store fingerprint failed", result);
      return;
    }
    ++template_count;
    ShowStorage();
    (void)snprintf(text, sizeof(text), "Saved fingerprint ID %u", (unsigned)enrollment_id);
    ShowLine(100, text);
    Message(text);
    SetState(WAIT_FOR_REMOVAL, "Enrolled: lift finger");
    return;
  }

  /* ZW3020 uses Search 0x04, not the unsupported HiSpeedSearch 0x1B. */
  uint8_t search[5] = {1, 0, 0, (uint8_t)(capacity >> 8), (uint8_t)capacity};
  result = Command(0x04, search, 5, 4);
  if (result == 0)
  {
    uint16_t id = ReadU16(&response[10]);
    uint16_t score = ReadU16(&response[12]);
    (void)snprintf(text, sizeof(text), "MATCH ID=%u score=%u", (unsigned)id, (unsigned)score);
    ShowLine(100, text);
    Message(text);
  }
  else if (result == 0x09 || result == 0x24)
  {
    ShowLine(100, result == 0x24 ? "Storage empty: PA0 to enroll" : "No matching fingerprint");
    Message(result == 0x24 ? "Storage empty: PA0 to enroll" : "No matching fingerprint");
  }
  else
  {
    Failure("Search fingerprint failed", result);
    return;
  }
  SetState(WAIT_FOR_REMOVAL, "Lift finger for next scan");
}
