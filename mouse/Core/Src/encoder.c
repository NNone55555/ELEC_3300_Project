#include "encoder.h"
#include "main.h"
#include <limits.h>

/* EC10E1220501 has 24 detents and 12 quadrature cycles per revolution. */
#define ENCODER_DIRECTION 1
#define STABLE_SAMPLES 2U

static volatile uint8_t enabled;
static volatile uint8_t raw_state;
static volatile int32_t scroll_steps;
static uint8_t candidate_state;
static uint8_t stable_samples;
static uint8_t previous_state;
static int8_t partial_step;

static uint8_t ReadPhases(void)
{
  uint32_t pins = ENC_A_GPIO_Port->IDR;
  uint8_t phase_a = (pins & ENC_A_Pin) != 0U;
  uint8_t phase_b = (pins & ENC_B_Pin) != 0U;
  return (uint8_t)((phase_a << 1) | phase_b);
}

void Encoder_Init(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  enabled = 0U;
  raw_state = ReadPhases();
  candidate_state = raw_state;
  previous_state = raw_state;
  stable_samples = STABLE_SAMPLES;
  partial_step = 0;
  scroll_steps = 0;
  enabled = 1U;
  __set_PRIMASK(interrupt_state);
}

void Encoder_Sample1ms(void)
{
  /* Called by TIM4. No HAL delays, display access or RTOS calls in this ISR. */
  if (enabled == 0U)
  {
    return;
  }
  raw_state = ReadPhases();
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

  /* Valid reverse transitions cancel bounce. Resynchronize on a two-bit jump. */
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
    int8_t direction = ENCODER_DIRECTION;
    if (partial_step < 0)
    {
      direction = -ENCODER_DIRECTION;
    }
    partial_step = 0;
    if ((direction > 0 && scroll_steps < INT32_MAX)
        || (direction < 0 && scroll_steps > INT32_MIN))
    {
      scroll_steps += direction;
    }
  }
}

EncoderSnapshot Encoder_GetSnapshot(void)
{
  EncoderSnapshot snapshot;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  snapshot.steps = scroll_steps;
  snapshot.phase_a = raw_state >> 1;
  snapshot.phase_b = raw_state & 1U;
  __set_PRIMASK(interrupt_state);
  return snapshot;
}
