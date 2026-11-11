#include "encoder_test.h"
#include "main.h"
#include "lcd.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* EC10E1220501: 24 detents and 12 quadrature cycles per revolution.
 * Two valid transitions make one nominal detent. Set -1 to reverse the sign.
 */
#define ENCODER_DIRECTION 1
#define STABLE_SAMPLES 2U

static UART_HandleTypeDef *test_uart;
static volatile uint8_t sampling_enabled;
static volatile uint8_t raw_state;
static volatile int32_t scroll_count;
static volatile int8_t last_direction;
static volatile uint32_t last_step_tick;
static uint8_t candidate_state;
static uint8_t stable_samples;
static uint8_t previous_state;
static int8_t partial_step;
static uint32_t last_display_tick;

static uint8_t ReadState(void)
{
  /* Both phases come from the same port, so read them in one snapshot. */
  uint32_t pins = ENC_A_GPIO_Port->IDR;
  uint8_t phase_a = (pins & ENC_A_Pin) != 0U;
  uint8_t phase_b = (pins & ENC_B_Pin) != 0U;
  return (uint8_t)((phase_a << 1) | phase_b);
}

static void ShowLine(uint16_t y, const char *text)
{
  LCD_Clear(0, y, 240, HEIGHT_EN_CHAR, BACKGROUND);
  LCD_DrawString(0, y, text);
}

void Encoder_Test_Init(UART_HandleTypeDef *uart)
{
  test_uart = uart;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  sampling_enabled = 0U;
  raw_state = ReadState();
  candidate_state = previous_state = raw_state;
  stable_samples = STABLE_SAMPLES;
  partial_step = 0;
  scroll_count = 0;
  last_direction = 0;
  last_step_tick = HAL_GetTick();
  sampling_enabled = 1U;
  __set_PRIMASK(interrupt_state);

  LCD_DrawString(0, 20, "A=PC8 B=PC9 C=GND");
  LCD_DrawString(0, 40, "Turn scroll wheel slowly");
  ShowLine(80, "Count: 0");
  ShowLine(100, "Direction: idle");
  static const uint8_t message[] = "\r\nEC10E1220501 scroll test\r\n"
                                  "A=PC8 B=PC9 common C=GND; 115200 8N1\r\n";
  if (HAL_UART_Transmit(test_uart, message, sizeof(message) - 1U, 100) != HAL_OK)
  {
    ShowLine(160, "USART1 TX error");
  }
  last_display_tick = HAL_GetTick() - 100U;
}

void Encoder_Test_Sample(void)
{
  /* Interrupt context: GPIO read and decoder only. */
  if (sampling_enabled == 0U)
  {
    return;
  }
  raw_state = ReadState();
  if (raw_state != candidate_state)
  {
    candidate_state = raw_state;
    stable_samples = 1U;
    return;
  }
  if (stable_samples < STABLE_SAMPLES)
  {
    ++stable_samples;
  }
  if (stable_samples < STABLE_SAMPLES || candidate_state == previous_state)
  {
    return;
  }

  /* + sequence: 00 -> 01 -> 11 -> 10 -> 00. Reverse transitions cancel
   * contact bounce. A two-bit jump loses phase information, so resynchronize.
   */
  static const int8_t transitions[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0
  };
  if ((candidate_state ^ previous_state) == 3U)
  {
    partial_step = 0;
  }
  else
  {
    partial_step += transitions[(previous_state << 2) | candidate_state];
  }
  previous_state = candidate_state;

  if (partial_step >= 2 || partial_step <= -2)
  {
    int8_t direction = partial_step > 0 ? ENCODER_DIRECTION : -ENCODER_DIRECTION;
    partial_step = 0;
    if ((direction > 0 && scroll_count < INT32_MAX)
        || (direction < 0 && scroll_count > INT32_MIN))
    {
      scroll_count += direction;
    }
    last_direction = direction;
    last_step_tick = HAL_GetTick();
  }
}

void Encoder_Test_Poll(void)
{
  uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - last_display_tick) < 100U)
  {
    return;
  }
  last_display_tick = now;

  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  now = HAL_GetTick();
  int32_t count = scroll_count;
  int8_t direction = last_direction;
  uint8_t pins = raw_state;
  uint32_t step_tick = last_step_tick;
  __set_PRIMASK(interrupt_state);

  const char *direction_text = "idle";
  if (direction != 0 && (uint32_t)(now - step_tick) < 500U)
  {
    direction_text = direction > 0 ? "+" : "-";
  }
  char text[64];
  (void)snprintf(text, sizeof(text), "Count: %ld", (long)count);
  ShowLine(80, text);
  (void)snprintf(text, sizeof(text), "Direction: %s", direction_text);
  ShowLine(100, text);
  (void)snprintf(text, sizeof(text), "A=%u B=%u", (unsigned)(pins >> 1),
                 (unsigned)(pins & 1U));
  ShowLine(120, text);
  (void)snprintf(text, sizeof(text), "Count=%ld Dir=%s A=%u B=%u\r\n",
                 (long)count, direction_text, (unsigned)(pins >> 1),
                 (unsigned)(pins & 1U));
  if (HAL_UART_Transmit(test_uart, (const uint8_t *)text,
                        (uint16_t)strlen(text), 100) != HAL_OK)
  {
    ShowLine(160, "USART1 TX error");
  }
}
