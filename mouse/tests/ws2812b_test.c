/* Native model of TIM1 CCR preload + compare-triggered halfword DMA. It checks
 * emitted pulses through the real driver, not just its encoded byte values.
 * It cannot measure GPIO rise times, bus contention or the connected LEDs.
 */
#include "ws2812b.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

TIM_TypeDef test_tim1;
SPI_TypeDef test_spi1;
AFIO_TypeDef test_afio;
GPIO_TypeDef test_gpioa;
DMA_Channel_TypeDef test_dma2;
uint32_t test_primask;
static TIM_HandleTypeDef timer;
static DMA_HandleTypeDef dma;
static uint32_t tick, pclk2 = 72000000U, apb2_divider;
static uint32_t starts, stops;
static const uint16_t *dma_data;
static uint16_t dma_size;
static uint16_t copied_frame[24U * WS2812B_LED_COUNT + 300U];
static HAL_StatusTypeDef start_result;

uint32_t HAL_GetTick(void) {return tick;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return pclk2;}
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *config, uint32_t *latency)
{config->APB2CLKDivider = apb2_divider; *latency = 2;}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pin, GPIO_PinState value)
{assert(port == GPIOA && pin == GPIO_PIN_8 && value == GPIO_PIN_RESET); port->levels = 0;}
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config)
{
  assert(test_primask == 1U && port == GPIOA && config->Pin == GPIO_PIN_8);
  assert(config->Pull == GPIO_NOPULL && config->Speed == GPIO_SPEED_FREQ_HIGH);
  assert(port->levels == 0); port->mode = config->Mode;
}
HAL_TIM_ChannelStateTypeDef HAL_TIM_GetChannelState(const TIM_HandleTypeDef *handle, uint32_t channel)
{assert(channel == TIM_CHANNEL_1); return handle->channel_state;}
HAL_StatusTypeDef HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *handle, uint32_t channel,
                                        const uint32_t *data, uint16_t size)
{
  assert(test_primask == 1 && handle == &timer && channel == TIM_CHANNEL_1);
  assert(handle->channel_state == HAL_TIM_CHANNEL_STATE_READY);
  assert(handle->Instance->CCR1 == 0 && handle->Instance->CNT == 0 && handle->Instance->EGR == TIM_EGR_UG);
  assert((handle->Instance->CR1 & TIM_CR1_CEN) == 0 && GPIOA->mode == GPIO_MODE_AF_PP);
  assert(((uintptr_t)data % 4U) == 0U && dma.Init.MemDataAlignment == DMA_MDATAALIGN_HALFWORD);
  handle->channel_state = HAL_TIM_CHANNEL_STATE_BUSY;
  ++starts;
  /* HAL sets channel BUSY before a failed DMA start. Stop must recover it. */
  if (start_result != HAL_OK) return start_result;
  assert(size <= sizeof(copied_frame) / sizeof(copied_frame[0]));
  dma_data = (const uint16_t *)data; dma_size = size;
  memcpy(copied_frame, dma_data, size * sizeof(uint16_t));
  handle->Instance->CR1 |= TIM_CR1_CEN;
  handle->Instance->CCER |= 1U;
  handle->Instance->DIER |= TIM_DMA_CC1;
  dma.interrupts = 3U;
  dma.State = HAL_DMA_STATE_BUSY;
  dma.Instance->CCR = 1U;
  dma.Instance->CNDTR = size;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop_DMA(TIM_HandleTypeDef *handle, uint32_t channel)
{
  assert(test_primask == 1 && handle == &timer && channel == TIM_CHANNEL_1);
  assert(GPIOA->mode == GPIO_MODE_OUTPUT_PP && GPIOA->levels == 0);
  ++stops;
  handle->Instance->CR1 &= ~TIM_CR1_CEN;
  handle->Instance->CCER &= ~1U;
  handle->Instance->DIER &= ~TIM_DMA_CC1;
  handle->channel_state = HAL_TIM_CHANNEL_STATE_READY;
  dma.State = HAL_DMA_STATE_READY;
  dma.Instance->CCR = 0U;
  return HAL_OK;
}

static void ConfigureHardware(void)
{
  memset(&timer, 0, sizeof(timer)); memset(&dma, 0, sizeof(dma));
  memset(TIM1, 0, sizeof(*TIM1)); memset(SPI1, 0, sizeof(*SPI1)); AFIO->MAPR = 0;
  pclk2 = 72000000U; apb2_divider = RCC_HCLK_DIV1;
  timer.Instance = TIM1; timer.Init.Period = 89;
  timer.channel_state = HAL_TIM_CHANNEL_STATE_READY;
  TIM1->ARR = 89; TIM1->CCMR1 = TIM_OCMODE_PWM1 | TIM_CCMR1_OC1PE;
  dma.Instance = DMA1_Channel2; dma.Parent = &timer; dma.State = HAL_DMA_STATE_READY;
  dma.Init.Direction = DMA_MEMORY_TO_PERIPH; dma.Init.Mode = DMA_NORMAL;
  dma.Init.MemInc = DMA_MINC_ENABLE; dma.Init.PeriphInc = DMA_PINC_DISABLE;
  dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  timer.hdma[TIM_DMA_ID_CC1] = &dma;
  start_result = HAL_OK;
}

static void InitAt(uint32_t time)
{
  ConfigureHardware(); tick = time;
  assert(WS2812B_Init(&timer) == HAL_OK);
  assert(WS2812B_IsBusy() && GPIOA->mode == GPIO_MODE_OUTPUT_PP && GPIOA->levels == 0);
  uint32_t before = starts;
  WS2812B_Update(); assert(starts == before);
  ++tick; WS2812B_Update(); assert(starts == before);
  ++tick; /* Minimum reset hold complete. */
}

static void StartFrame(void)
{
  uint32_t before = starts;
  WS2812B_Update();
  assert(starts == before + 1U && WS2812B_GetStatus().state == WS2812B_TRANSMITTING);
  assert((dma.interrupts & DMA_IT_HT) == 0U && test_primask == 0);
  assert(dma_size == 2U + WS2812B_LED_COUNT * 24U + 256U + 2U);
}

/* Model one compare request per PWM period. DMA writes CCR1's preload;
 * the active compare changes at the next update, not at the DMA write.
 */
static void FinishAndDecode(uint8_t rgb[WS2812B_LED_COUNT][3])
{
  uint16_t shadow = 0U;
  unsigned bits = 0U;
  uint8_t decoded[3U * WS2812B_LED_COUNT] = {0};
  uint32_t last_falling_edge = 0U;
  assert(memcmp(copied_frame, dma_data, dma_size * sizeof(uint16_t)) == 0);
  for (uint16_t period = 0; period < dma_size; ++period)
  {
    uint16_t active = shadow;
    if (active != 0U)
    {
      assert(active == WS2812B_ZERO_TICKS || active == WS2812B_ONE_TICKS);
      assert(bits < WS2812B_LED_COUNT * 24U);
      decoded[bits / 8U] = (uint8_t)((decoded[bits / 8U] << 1)
                                    | (active == WS2812B_ONE_TICKS));
      ++bits;
      last_falling_edge = period * 90U + active;
    }
    shadow = dma_data[period];
  }
  assert(bits == WS2812B_LED_COUNT * 24U);
  uint32_t complete_at = (dma_size - 1U) * 90U; /* Final CC event has active=0. */
  assert(complete_at - last_falling_edge >= 320U * 72U);
  assert(shadow == 0U);
  for (unsigned led = 0; led < WS2812B_LED_COUNT; ++led)
  {
    rgb[led][0] = decoded[led * 3U + 1U];
    rgb[led][1] = decoded[led * 3U + 0U];
    rgb[led][2] = decoded[led * 3U + 2U];
  }
  /* Match the HAL's normal-mode channel-ready/completion callback order. */
  dma.State = HAL_DMA_STATE_READY; timer.channel_state = HAL_TIM_CHANNEL_STATE_READY;
  timer.Channel = HAL_TIM_ACTIVE_CHANNEL_1;
  WS2812B_OnPulseFinished(&timer);
  timer.Channel = 0U;
  assert(WS2812B_GetStatus().state == WS2812B_RESETTING && WS2812B_IsBusy());
  assert(GPIOA->mode == GPIO_MODE_OUTPUT_PP && GPIOA->levels == 0 && dma.Instance->CCR == 0);
  assert(TIM1->CCR1 == 0 && (TIM1->CR1 & TIM_CR1_CEN) == 0 && test_primask == 0);
}

static void SetStatic(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
  WS2812B_Config config = {WS2812B_STATIC, r, g, b, brightness, 1000};
  assert(WS2812B_SetConfig(&config) == HAL_OK);
}

static void TestConfiguration(void)
{
  /* Nanosecond inequalities from the V5 timing table, without float rounding. */
  assert(WS2812B_ZERO_TICKS * 1000U >= 220U * 72U && WS2812B_ZERO_TICKS * 1000U <= 380U * 72U);
  assert(WS2812B_ONE_TICKS * 1000U >= 580U * 72U && WS2812B_ONE_TICKS * 1000U <= 1000U * 72U);
  assert((90U - WS2812B_ZERO_TICKS) * 1000U >= 580U * 72U);
  assert((90U - WS2812B_ONE_TICKS) * 1000U >= 580U * 72U);
  assert(WS2812B_GetStatus().state == WS2812B_UNINITIALIZED);
  ConfigureHardware();
  assert(WS2812B_Init(NULL) == HAL_ERROR);
  pclk2 = 36000000; assert(WS2812B_Init(&timer) == HAL_ERROR);
  apb2_divider = RCC_HCLK_DIV2; assert(WS2812B_Init(&timer) == HAL_OK);
  ConfigureHardware(); TIM1->CCMR1 &= ~TIM_CCMR1_OC1PE;
  assert(WS2812B_Init(&timer) == HAL_ERROR);
  ConfigureHardware(); dma.Init.MemDataAlignment = 4;
  assert(WS2812B_Init(&timer) == HAL_ERROR);
  ConfigureHardware(); dma.Init.Mode = 1;
  assert(WS2812B_Init(&timer) == HAL_ERROR);
  ConfigureHardware(); TIM1->CR2 |= TIM_CR2_CCDS;
  assert(WS2812B_Init(&timer) == HAL_ERROR);
  ConfigureHardware(); AFIO->MAPR = AFIO_MAPR_TIM1_REMAP;
  assert(WS2812B_Init(&timer) == HAL_ERROR);
  InitAt(0);
  assert(WS2812B_SetMode((WS2812B_Mode)-1) == HAL_ERROR);
  assert(WS2812B_SetBrightness(101) == HAL_ERROR);
  assert(WS2812B_SetPeriodMs(0) == HAL_ERROR && WS2812B_SetPeriodMs(60001) == HAL_ERROR);
  assert(WS2812B_SetConfig(NULL) == HAL_ERROR);
  puts("PASS: timer clock/APB multiplier, PWM preload, DMA width/mode, remap and configuration limits.");
}

static void TestColorsAndBusy(void)
{
  uint8_t rgb[WS2812B_LED_COUNT][3];
  const uint8_t colors[][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255},{0x81,0x42,0x24}};
  for (unsigned color = 0; color < sizeof(colors) / sizeof(colors[0]); ++color)
  {
    SetStatic(colors[color][0], colors[color][1], colors[color][2], 100);
    StartFrame();
    uint32_t before = starts;
    WS2812B_Update(); WS2812B_Update();
    assert(starts == before && WS2812B_Init(&timer) == HAL_BUSY);
    TIM_HandleTypeDef unrelated = {0}; WS2812B_OnPulseFinished(&unrelated);
    timer.Channel = HAL_TIM_ACTIVE_CHANNEL_2; WS2812B_OnPulseFinished(&timer);
    assert(WS2812B_GetStatus().state == WS2812B_TRANSMITTING);
    FinishAndDecode(rgb);
    for (unsigned led = 0; led < WS2812B_LED_COUNT; ++led)
      assert(memcmp(rgb[led], colors[color], 3) == 0);
    tick += 2;
    WS2812B_Update(); assert(starts == before && !WS2812B_IsBusy());
  }
  SetStatic(255, 80, 1, 50); StartFrame();
  WS2812B_SetColor(0, 0, 255); /* Must not alter an in-flight buffer. */
  FinishAndDecode(rgb); assert(rgb[0][0] == 128 && rgb[0][1] == 40 && rgb[0][2] == 1);
  tick += 2; StartFrame(); FinishAndDecode(rgb); assert(rgb[0][0] == 0 && rgb[0][2] == 128);
  tick += 2; assert(WS2812B_SetMode(WS2812B_OFF) == HAL_OK);
  StartFrame(); FinishAndDecode(rgb);
  for (unsigned led = 0; led < WS2812B_LED_COUNT; ++led)
    assert(rgb[led][0] == 0 && rgb[led][1] == 0 && rgb[led][2] == 0);
  tick += 2;
  puts("PASS: modeled GRB/MSB wave decode, first/last bits, static colors, brightness rounding, OFF frame, no overlap, immutable DMA buffer and LOW reset >=320 us.");
}

static void TestAnimations(void)
{
  uint8_t rgb[WS2812B_LED_COUNT][3];
  WS2812B_Config config = {WS2812B_BREATHING, 0, 0, 255, 100, 1000};
  assert(WS2812B_SetConfig(&config) == HAL_OK);
  uint32_t start = tick;
  const uint8_t expected[] = {0,128,255,128,0};
  for (unsigned quarter = 0; quarter < 5; ++quarter)
  {
    tick = start + quarter * 250U; StartFrame(); FinishAndDecode(rgb);
    assert(rgb[0][0] == 0 && rgb[0][1] == 0);
    assert(rgb[0][2] >= (expected[quarter] == 0 ? 0 : expected[quarter] - 1)
           && rgb[0][2] <= expected[quarter]);
  }
  tick += 2; config.mode = WS2812B_RAINBOW; config.period_ms = 1200;
  assert(WS2812B_SetConfig(&config) == HAL_OK);
  start = tick; StartFrame(); FinishAndDecode(rgb);
  assert(rgb[0][0] == 255 && rgb[0][1] == 0 && rgb[0][2] == 0);
  if (WS2812B_LED_COUNT == 8U)
    assert(rgb[4][0] == 0 && rgb[4][1] == 255 && rgb[4][2] == 255);
  tick = start + 600; StartFrame(); FinishAndDecode(rgb);
  assert(rgb[0][0] == 0 && rgb[0][1] == 255 && rgb[0][2] == 255);
  tick += 2; assert(WS2812B_SetPeriodMs(600) == HAL_OK);
  StartFrame(); FinishAndDecode(rgb);
  tick += 300; StartFrame(); FinishAndDecode(rgb);
  assert(rgb[0][0] == 255 && rgb[0][1] <= 6 && rgb[0][2] == 0);
  tick += 2; assert(WS2812B_SetBrightness(0) == HAL_OK);
  StartFrame(); FinishAndDecode(rgb);
  for (unsigned led = 0; led < WS2812B_LED_COUNT; ++led)
    assert(rgb[led][0] == 0 && rgb[led][1] == 0 && rgb[led][2] == 0);
  tick += 2;
  /* A large speed change preserves hue rather than reusing the old remainder
   * as if it belonged to the much shorter new cycle period.
   */
  assert(WS2812B_SetMode(WS2812B_OFF) == HAL_OK);
  InitAt(4000); config.brightness = 100; config.period_ms = 60000;
  assert(WS2812B_SetConfig(&config) == HAL_OK);
  start = tick; StartFrame(); FinishAndDecode(rgb);
  tick = start + 20U; StartFrame(); FinishAndDecode(rgb);
  tick = start + 23U; WS2812B_Update();
  assert(WS2812B_SetPeriodMs(100U) == HAL_OK);
  StartFrame(); FinishAndDecode(rgb);
  assert(rgb[0][0] == 255 && rgb[0][1] == 0 && rgb[0][2] == 0);
  puts("PASS: eased breathing cycle, spatial/animated rainbow, adjustable speed and zero global brightness.");
}

static void TestRecoveryAndWrap(void)
{
  uint8_t rgb[WS2812B_LED_COUNT][3];
  InitAt(3000); SetStatic(255, 0, 0, 100);
  start_result = HAL_ERROR; uint32_t before = starts;
  WS2812B_Update(); assert(starts == before + 1 && WS2812B_GetStatus().last_error == WS2812B_ERROR_DMA_START);
  assert(GPIOA->mode == GPIO_MODE_OUTPUT_PP && timer.channel_state == HAL_TIM_CHANNEL_STATE_READY);
  start_result = HAL_OK; tick += 2; StartFrame();
  timer.Channel = HAL_TIM_ACTIVE_CHANNEL_1; WS2812B_OnTimerError(&timer); timer.Channel = 0;
  assert(WS2812B_GetStatus().last_error == WS2812B_ERROR_DMA_TRANSFER && GPIOA->levels == 0);
  tick += 2; StartFrame();
  tick += 25; WS2812B_Update();
  assert(WS2812B_GetStatus().last_error == WS2812B_ERROR_TIMEOUT && !dma.Instance->CCR);
  assert(WS2812B_GetStatus().frames_completed == 0);
  tick += 2; SPI1->CR2 = SPI_CR2_RXDMAEN;
  before = starts; uint32_t stops_before = stops; WS2812B_Update();
  assert(starts == before && stops == stops_before);
  assert(WS2812B_GetStatus().last_error == WS2812B_ERROR_DMA_CONFLICT);
  SPI1->CR2 = 0; tick += 2; StartFrame(); FinishAndDecode(rgb);
  assert(WS2812B_GetStatus().frames_completed == 1 && WS2812B_GetStatus().errors == 4);
  InitAt(UINT32_MAX - 1U); SetStatic(0, 255, 0, 100);
  StartFrame(); FinishAndDecode(rgb); tick += 2;
  WS2812B_SetColor(0, 0, 255); StartFrame(); FinishAndDecode(rgb);
  assert(rgb[0][2] == 255 && WS2812B_GetStatus().frames_completed == 2);
  puts("PASS: failed DMA-start/channel recovery, DMA error, missing-IRQ timeout, SPI1 RX conflict, successful retry and tick wrap.");
}

int main(void)
{
  TestConfiguration(); TestColorsAndBusy(); TestAnimations(); TestRecoveryAndWrap();
  return 0;
}
