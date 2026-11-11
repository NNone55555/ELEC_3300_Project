#include "mouse_buttons.h"
#include "main.h"

#define BUTTON_COUNT 5U
#define DEBOUNCE_SAMPLES 5U

static volatile uint8_t pin_levels;
static volatile uint8_t pressed;
static uint8_t stable_samples[BUTTON_COUNT];

static uint8_t ReadPinLevels(void)
{
  uint8_t levels = 0U;
  if (HAL_GPIO_ReadPin(BTN_LEFT_GPIO_Port, BTN_LEFT_Pin) == GPIO_PIN_SET)
    levels |= MOUSE_BUTTON_LEFT;
  if (HAL_GPIO_ReadPin(BTN_RIGHT_GPIO_Port, BTN_RIGHT_Pin) == GPIO_PIN_SET)
    levels |= MOUSE_BUTTON_RIGHT;
  if (HAL_GPIO_ReadPin(BTN_MIDDLE_GPIO_Port, BTN_MIDDLE_Pin) == GPIO_PIN_SET)
    levels |= MOUSE_BUTTON_MIDDLE;
  if (HAL_GPIO_ReadPin(BTN_SIDE1_GPIO_Port, BTN_SIDE1_Pin) == GPIO_PIN_SET)
    levels |= MOUSE_BUTTON_SIDE1;
  if (HAL_GPIO_ReadPin(BTN_SIDE2_GPIO_Port, BTN_SIDE2_Pin) == GPIO_PIN_SET)
    levels |= MOUSE_BUTTON_SIDE2;
  return levels;
}

void MouseButtons_Init(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  pin_levels = ReadPinLevels();
  pressed = (uint8_t)(~pin_levels & 0x1FU);
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i)
    stable_samples[i] = 0U;
  __set_PRIMASK(interrupt_state);
}

void MouseButtons_Sample1ms(void)
{
  pin_levels = ReadPinLevels();
  uint8_t raw_pressed = (uint8_t)(~pin_levels & 0x1FU);
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i)
  {
    uint8_t mask = 1U << i;
    if (((raw_pressed ^ pressed) & mask) == 0U)
    {
      stable_samples[i] = 0U;
    }
    else if (++stable_samples[i] >= DEBOUNCE_SAMPLES)
    {
      pressed ^= mask;
      stable_samples[i] = 0U;
    }
  }
}

MouseButtonsSnapshot MouseButtons_GetSnapshot(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  MouseButtonsSnapshot snapshot = {pressed, pin_levels};
  __set_PRIMASK(interrupt_state);
  return snapshot;
}

