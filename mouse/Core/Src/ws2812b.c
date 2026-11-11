#include "ws2812b.h"
#include <string.h>

#define TIMER_CLOCK_HZ 72000000U
#define PERIOD_TICKS 90U
#define FRAME_INTERVAL_MS 20U
#define RESET_GUARD_MS 2U /* At least 1 real ms even at a HAL tick boundary. */
#define LEADING_SLOTS 2U
#define RESET_SLOTS 256U /* 320 us, plus pipeline padding below. */
#define PIPELINE_PADDING 2U
#define DATA_SLOTS (WS2812B_LED_COUNT * 24U)
#define BUFFER_SLOTS (LEADING_SLOTS + DATA_SLOTS + RESET_SLOTS + PIPELINE_PADDING)
#define DMA_TIMEOUT_MS (((BUFFER_SLOTS * 1250U + 999999U) / 1000000U) + 10U)

_Static_assert(WS2812B_LED_COUNT >= 1U && WS2812B_LED_COUNT <= 256U, "LED count must be 1..256");
_Static_assert(BUFFER_SLOTS <= UINT16_MAX, "DMA length exceeds HAL limit");
_Static_assert(WS2812B_ZERO_TICKS > 0U && WS2812B_ZERO_TICKS < WS2812B_ONE_TICKS
               && WS2812B_ONE_TICKS < PERIOD_TICKS, "Invalid PWM duties");

/* HAL's uint32_t* parameter is an address: the linked DMA transfers halfwords. */
_Alignas(4) static uint16_t pwm_buffer[BUFFER_SLOTS];
static TIM_HandleTypeDef *strip_timer;
static WS2812B_Config desired = {WS2812B_OFF, 255, 255, 255, 100, 3000};
static uint32_t revision = 1U;
static uint32_t submitted_revision;
static volatile WS2812B_State state;
static volatile WS2812B_Error last_error;
static volatile uint32_t frames_completed;
static volatile uint32_t errors;
static volatile uint32_t reset_started;
static uint32_t transfer_started;
static uint32_t last_frame;
static uint32_t phase_updated;
static uint32_t phase_remainder;
static uint32_t phase_period_ms;
static uint16_t phase;
static WS2812B_Mode animation_mode;

static void ConfigureOutput(uint32_t mode)
{
  GPIO_InitTypeDef pin = {0};
  /* Set ODR before switching from alternate function to GPIO output. */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
  pin.Pin = GPIO_PIN_8;
  pin.Mode = mode;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &pin);
}

static bool HardwareIsValid(TIM_HandleTypeDef *timer)
{
  if (timer == NULL || timer->Instance != TIM1 || timer->hdma[TIM_DMA_ID_CC1] == NULL)
    return false;
  DMA_HandleTypeDef *dma = timer->hdma[TIM_DMA_ID_CC1];
  RCC_ClkInitTypeDef clocks = {0};
  uint32_t latency;
  HAL_RCC_GetClockConfig(&clocks, &latency);
  uint32_t timer_clock = HAL_RCC_GetPCLK2Freq();
  if (clocks.APB2CLKDivider != RCC_HCLK_DIV1)
    timer_clock *= 2U;

  return timer_clock == TIMER_CLOCK_HZ
         && timer->Init.Prescaler == 0U && timer->Init.Period == PERIOD_TICKS - 1U
         && timer->Init.CounterMode == TIM_COUNTERMODE_UP && timer->Init.RepetitionCounter == 0U
         && timer->Instance->PSC == 0U && timer->Instance->ARR == PERIOD_TICKS - 1U
         && (timer->Instance->CR1 & (TIM_CR1_CEN | TIM_CR1_DIR | TIM_CR1_CMS)) == 0U
         && (timer->Instance->CR2 & TIM_CR2_CCDS) == 0U
         && (timer->Instance->CCMR1 & (TIM_CCMR1_CC1S | TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE))
              == (TIM_OCMODE_PWM1 | TIM_CCMR1_OC1PE)
         && (timer->Instance->CCER & (TIM_CCER_CCxE_MASK | TIM_CCER_CCxNE_MASK | TIM_CCER_CC1P)) == 0U
         && (AFIO->MAPR & AFIO_MAPR_TIM1_REMAP) == 0U
         && dma->Instance == DMA1_Channel2 && dma->Parent == timer
         && dma->State == HAL_DMA_STATE_READY
         && HAL_TIM_GetChannelState(timer, TIM_CHANNEL_1) == HAL_TIM_CHANNEL_STATE_READY
         && dma->Init.Direction == DMA_MEMORY_TO_PERIPH && dma->Init.Mode == DMA_NORMAL
         && dma->Init.MemInc == DMA_MINC_ENABLE && dma->Init.PeriphInc == DMA_PINC_DISABLE
         && dma->Init.MemDataAlignment == DMA_MDATAALIGN_HALFWORD
         && dma->Init.PeriphDataAlignment == DMA_PDATAALIGN_HALFWORD;
}

/* Called with interrupts masked, including from the matching DMA callback. */
static void StopAndReset(WS2812B_Error error)
{
  /* GPIO LOW is explicit: stopping advanced-timer PWM alone may leave AF Hi-Z. */
  ConfigureOutput(GPIO_MODE_OUTPUT_PP);
  (void)HAL_TIM_PWM_Stop_DMA(strip_timer, TIM_CHANNEL_1);
  __HAL_DMA_DISABLE(strip_timer->hdma[TIM_DMA_ID_CC1]);
  __HAL_TIM_SET_COMPARE(strip_timer, TIM_CHANNEL_1, 0U);
  if (error != WS2812B_ERROR_NONE)
  {
    last_error = error;
    ++errors;
    submitted_revision = revision - 1U; /* Retry this static frame after reset. */
  }
  reset_started = HAL_GetTick();
  state = WS2812B_RESETTING;
}

HAL_StatusTypeDef WS2812B_Init(TIM_HandleTypeDef *timer)
{
  if (state == WS2812B_TRANSMITTING || state == WS2812B_PREPARING)
    return HAL_BUSY;
  if (!HardwareIsValid(timer))
  {
    last_error = WS2812B_ERROR_HARDWARE_CONFIG;
    ++errors;
    return HAL_ERROR;
  }
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  strip_timer = timer;
  ConfigureOutput(GPIO_MODE_OUTPUT_PP);
  /* Prefix/reset slots are zero-initialized and never written by EncodeFrame. */
  frames_completed = errors = 0U;
  last_error = WS2812B_ERROR_NONE;
  submitted_revision = revision - 1U;
  phase = 0U;
  phase_remainder = 0U;
  phase_period_ms = desired.period_ms;
  animation_mode = desired.mode;
  phase_updated = reset_started = HAL_GetTick();
  state = WS2812B_RESETTING;
  __set_PRIMASK(interrupt_state);
  return HAL_OK;
}

WS2812B_Config WS2812B_GetConfig(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  WS2812B_Config snapshot = desired;
  __set_PRIMASK(interrupt_state);
  return snapshot;
}

HAL_StatusTypeDef WS2812B_SetConfig(const WS2812B_Config *config)
{
  if (config == NULL || (unsigned)config->mode > WS2812B_RAINBOW
      || config->brightness > 100U || config->period_ms < 100U || config->period_ms > 60000U)
    return HAL_ERROR;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  desired = *config;
  ++revision;
  __set_PRIMASK(interrupt_state);
  return HAL_OK;
}

HAL_StatusTypeDef WS2812B_SetMode(WS2812B_Mode mode)
{
  if ((unsigned)mode > WS2812B_RAINBOW)
    return HAL_ERROR;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  desired.mode = mode;
  ++revision;
  __set_PRIMASK(interrupt_state);
  return HAL_OK;
}

void WS2812B_SetColor(uint8_t red, uint8_t green, uint8_t blue)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  desired.red = red;
  desired.green = green;
  desired.blue = blue;
  ++revision;
  __set_PRIMASK(interrupt_state);
}

HAL_StatusTypeDef WS2812B_SetBrightness(uint8_t percent)
{
  if (percent > 100U)
    return HAL_ERROR;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  desired.brightness = percent;
  ++revision;
  __set_PRIMASK(interrupt_state);
  return HAL_OK;
}

HAL_StatusTypeDef WS2812B_SetPeriodMs(uint32_t period_ms)
{
  if (period_ms < 100U || period_ms > 60000U)
    return HAL_ERROR;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  desired.period_ms = period_ms;
  ++revision;
  __set_PRIMASK(interrupt_state);
  return HAL_OK;
}

static uint32_t BreathingEnvelope(void)
{
  uint32_t triangle = phase < 32768U ? 2U * phase : 2U * (65535U - phase);
  /* Smoothstep eases both ends of the triangle, without floating point. */
  uint32_t squared = ((uint64_t)triangle * triangle + 32767U) / 65535U;
  return ((uint64_t)squared * (196605U - 2U * triangle) + 32767U) / 65535U;
}

static void RainbowColor(uint16_t index, uint8_t rgb[3])
{
  uint16_t hue = (((uint32_t)phase * 1536U) >> 16)
                 + ((uint32_t)index * 1536U) / WS2812B_LED_COUNT;
  hue %= 1536U;
  uint8_t ramp = hue & 0xFFU;
  switch (hue >> 8)
  {
    case 0: rgb[0] = 255; rgb[1] = ramp; rgb[2] = 0; break;
    case 1: rgb[0] = 255 - ramp; rgb[1] = 255; rgb[2] = 0; break;
    case 2: rgb[0] = 0; rgb[1] = 255; rgb[2] = ramp; break;
    case 3: rgb[0] = 0; rgb[1] = 255 - ramp; rgb[2] = 255; break;
    case 4: rgb[0] = ramp; rgb[1] = 0; rgb[2] = 255; break;
    default: rgb[0] = 255; rgb[1] = 0; rgb[2] = 255 - ramp; break;
  }
}

static void EncodeFrame(const WS2812B_Config *config)
{
  uint32_t envelope = config->mode == WS2812B_BREATHING ? BreathingEnvelope() : 65535U;
  uint32_t offset = LEADING_SLOTS;
  for (uint16_t led = 0; led < WS2812B_LED_COUNT; ++led)
  {
    uint8_t rgb[3] = {config->red, config->green, config->blue};
    if (config->mode == WS2812B_OFF)
      memset(rgb, 0, sizeof(rgb));
    else if (config->mode == WS2812B_RAINBOW)
      RainbowColor(led, rgb);
    for (uint8_t component = 0; component < 3U; ++component)
      rgb[component] = ((uint64_t)rgb[component] * config->brightness * envelope + 3276750U) / 6553500U;
    const uint8_t grb[3] = {rgb[1], rgb[0], rgb[2]};
    for (uint8_t component = 0; component < 3U; ++component)
      for (uint8_t bit = 0x80U; bit != 0U; bit >>= 1)
        pwm_buffer[offset++] = (grb[component] & bit) != 0U ? WS2812B_ONE_TICKS : WS2812B_ZERO_TICKS;
  }
  /* Prefix and tail remain zero. Data and reset padding are separate. */
}

void WS2812B_Update(void)
{
  if (strip_timer == NULL)
    return;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  uint32_t now = HAL_GetTick();
  WS2812B_Config config = desired;
  uint32_t current_revision = revision;
  if (state == WS2812B_TRANSMITTING && (uint32_t)(now - transfer_started) >= DMA_TIMEOUT_MS)
    StopAndReset(WS2812B_ERROR_TIMEOUT);
  if (state == WS2812B_RESETTING && (uint32_t)(now - reset_started) >= RESET_GUARD_MS)
    state = WS2812B_IDLE;
  bool ready = state == WS2812B_IDLE;
  __set_PRIMASK(interrupt_state);

  if (config.mode != animation_mode)
  {
    phase = 0U;
    phase_remainder = 0U;
    animation_mode = config.mode;
    phase_updated = now;
  }
  if (config.period_ms != phase_period_ms)
  {
    phase_remainder = ((uint64_t)phase_remainder * config.period_ms) / phase_period_ms;
    phase_period_ms = config.period_ms;
  }
  uint64_t advance = (uint64_t)(uint32_t)(now - phase_updated) * 65536U + phase_remainder;
  phase = (uint16_t)(phase + advance / config.period_ms);
  phase_remainder = advance % config.period_ms;
  phase_updated = now;
  bool animated = config.mode == WS2812B_BREATHING || config.mode == WS2812B_RAINBOW;
  if (!ready || (current_revision == submitted_revision
                 && (!animated || (uint32_t)(now - last_frame) < FRAME_INTERVAL_MS)))
    return;

  /* A single task owns Update/Init; setters only touch desired settings. */
  state = WS2812B_PREPARING;
  EncodeFrame(&config); /* IRQs remain enabled throughout the serialization. */
  interrupt_state = __get_PRIMASK();
  __disable_irq();
  now = HAL_GetTick();
  if ((SPI1->CR2 & SPI_CR2_RXDMAEN) != 0U)
  {
    /* Do not abort/reprogram DMA1_Channel2 if SPI1 RX is using it. */
    ConfigureOutput(GPIO_MODE_OUTPUT_PP);
    last_error = WS2812B_ERROR_DMA_CONFLICT;
    ++errors;
    reset_started = now;
    state = WS2812B_RESETTING;
    __set_PRIMASK(interrupt_state);
    return;
  }
  __HAL_TIM_DISABLE_DMA(strip_timer, TIM_DMA_CC1);
  __HAL_TIM_SET_COMPARE(strip_timer, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COUNTER(strip_timer, 0U);
  /* Transfer zero into CCR1's active shadow before enabling the output.
   * With OC1PE enabled, CC1 DMA writes each following duty for the next period.
   */
  strip_timer->Instance->EGR = TIM_EGR_UG;
  __HAL_TIM_CLEAR_FLAG(strip_timer, TIM_FLAG_UPDATE | TIM_FLAG_CC1);
  ConfigureOutput(GPIO_MODE_AF_PP);
  transfer_started = now;
  state = WS2812B_TRANSMITTING;
  if (HAL_TIM_PWM_Start_DMA(strip_timer, TIM_CHANNEL_1,
                            (const uint32_t *)pwm_buffer, BUFFER_SLOTS) != HAL_OK)
    StopAndReset(WS2812B_ERROR_DMA_START);
  else
  {
    __HAL_DMA_DISABLE_IT(strip_timer->hdma[TIM_DMA_ID_CC1], DMA_IT_HT);
    submitted_revision = current_revision;
    last_frame = now;
  }
  __set_PRIMASK(interrupt_state);
}

void WS2812B_OnPulseFinished(TIM_HandleTypeDef *timer)
{
  if (timer != strip_timer || timer == NULL || timer->Channel != HAL_TIM_ACTIVE_CHANNEL_1)
    return;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  if (state == WS2812B_TRANSMITTING)
  {
    ++frames_completed;
    StopAndReset(WS2812B_ERROR_NONE);
  }
  __set_PRIMASK(interrupt_state);
}

void WS2812B_OnTimerError(TIM_HandleTypeDef *timer)
{
  if (timer != strip_timer || timer == NULL || timer->Channel != HAL_TIM_ACTIVE_CHANNEL_1)
    return;
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  if (state == WS2812B_TRANSMITTING)
    StopAndReset(WS2812B_ERROR_DMA_TRANSFER);
  __set_PRIMASK(interrupt_state);
}

WS2812B_Status WS2812B_GetStatus(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  WS2812B_Status status = {state, last_error, frames_completed, errors};
  __set_PRIMASK(interrupt_state);
  return status;
}

bool WS2812B_IsBusy(void)
{
  WS2812B_State current = state;
  return current == WS2812B_PREPARING || current == WS2812B_TRANSMITTING || current == WS2812B_RESETTING;
}
