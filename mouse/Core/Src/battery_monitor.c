#include "battery_monitor.h"
#include "ws2812b.h"

#ifndef BATTERY_POT_DEMO
#define BATTERY_POT_DEMO 1 /* Uncomment this line for demo using potentiometer */
#endif

#define ADC_MAX_COUNT 4095U
#define SAMPLE_INTERVAL_MS 1000U
#define LOW_BATTERY_PERCENT 15U

#if !BATTERY_POT_DEMO
#ifndef BATTERY_ADC_REF_MV
#define BATTERY_ADC_REF_MV 3300U /* MINI V3 VREF+, adjustable after measurement. */
#endif
#define DIVIDER_GAIN 11U /* (100 kOhm + 10 kOhm) / 10 kOhm */
#define PACK_PRESENT_MV 4000U
#define PACK_EMPTY_MV 8000U
#define PACK_FULL_MV 12400U
#endif

static ADC_HandleTypeDef *battery_adc;
static uint32_t sample_started;
static bool conversion_pending;
/* One aligned word shared between the LED task and LCD task. */
static volatile int32_t raw_adc = -1;

int32_t BatteryMonitor_GetRaw(void)
{
  return raw_adc;
}

#if !BATTERY_POT_DEMO
/* Simple voltage-based estimate for eight alkaline cells in series. */
static uint8_t EstimatePercent(uint32_t pack_mv)
{
  if (pack_mv <= PACK_EMPTY_MV)
    return 0U;
  if (pack_mv >= PACK_FULL_MV)
    return 100U;
  return (pack_mv - PACK_EMPTY_MV) * 100U / (PACK_FULL_MV - PACK_EMPTY_MV);
}
#endif

bool BatteryMonitor_Init(ADC_HandleTypeDef *adc)
{
  if (HAL_ADCEx_Calibration_Start(adc) != HAL_OK)
    return false;

  battery_adc = adc;
  sample_started = HAL_GetTick() - SAMPLE_INTERVAL_MS;
  return true;
}

void BatteryMonitor_Update(void)
{
  if (battery_adc == NULL)
    return;

  if (!conversion_pending)
  {
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - sample_started) < SAMPLE_INTERVAL_MS)
      return;

    sample_started = now;
    conversion_pending = HAL_ADC_Start(battery_adc) == HAL_OK;
    if (!conversion_pending)
    {
      raw_adc = -1;
      (void)HAL_ADC_Stop(battery_adc);
      WS2812B_SetBatteryIndicator(0U, false);
    }
    return;
  }

  /* A conversion takes 21 us at 12 MHz. Check it on the next LED task iteration
   * with a zero timeout, so the task never waits for an ADC result.
   */
  conversion_pending = false;
  bool valid = HAL_ADC_PollForConversion(battery_adc, 0U) == HAL_OK;
  uint32_t raw = valid ? HAL_ADC_GetValue(battery_adc) : 0U;
  raw_adc = valid ? (int32_t)raw : -1;
  HAL_StatusTypeDef stop_result = HAL_ADC_Stop(battery_adc);
  if (!valid || stop_result != HAL_OK || raw > ADC_MAX_COUNT)
  {
    WS2812B_SetBatteryIndicator(0U, false);
    return;
  }

#if BATTERY_POT_DEMO
  uint8_t percent = raw * 100U / ADC_MAX_COUNT;
#else
  uint32_t pack_mv = (raw * BATTERY_ADC_REF_MV * DIVIDER_GAIN + ADC_MAX_COUNT / 2U) / ADC_MAX_COUNT;
  if (raw == ADC_MAX_COUNT || pack_mv < PACK_PRESENT_MV)
  {
    WS2812B_SetBatteryIndicator(0U, false);
    return;
  }

  uint8_t percent = EstimatePercent(pack_mv);
#endif
  uint8_t bars = (percent >= 67U) ? 3U : (percent >= 34U) ? 2U : 1U;
  WS2812B_SetBatteryIndicator(bars, percent < LOW_BATTERY_PERCENT);
}
