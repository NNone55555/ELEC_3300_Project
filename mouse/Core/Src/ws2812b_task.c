#include "ws2812b_task.h"
#include "ws2812b.h"
#include "cmsis_os2.h"

#ifndef MOUSE_WS2812B_DEMO
#define MOUSE_WS2812B_DEMO 1
#endif

#if MOUSE_WS2812B_DEMO
typedef enum {
  DEMO_RED,
  DEMO_GREEN,
  DEMO_BLUE,
  DEMO_WHITE,
  DEMO_OFF,
  DEMO_BREATHING,
  DEMO_RAINBOW,
  DEMO_STAGE_COUNT
} DemoStage;

static const uint32_t stage_duration_ms[DEMO_STAGE_COUNT] = {
  2000, 2000, 2000, 2000, 1000, 6000, 8000
};

static void ApplyStage(DemoStage stage)
{
  WS2812B_Config config = {WS2812B_STATIC, 0, 0, 0, 20, 3000};
  switch (stage)
  {
    case DEMO_RED: config.red = 255; break;
    case DEMO_GREEN: config.green = 255; break;
    case DEMO_BLUE: config.blue = 255; break;
    case DEMO_WHITE: config.red = config.green = config.blue = 255; break;
    case DEMO_OFF: config.mode = WS2812B_OFF; break;
    case DEMO_BREATHING: config.mode = WS2812B_BREATHING; config.blue = 255; break;
    case DEMO_RAINBOW: config.mode = WS2812B_RAINBOW; config.period_ms = 4000; break;
    default: break;
  }
  (void)WS2812B_SetConfig(&config);
}
#endif

static void LedTask(void *argument)
{
  (void)argument;
#if MOUSE_WS2812B_DEMO
  DemoStage stage = DEMO_RED;
  uint32_t stage_started = HAL_GetTick();
  ApplyStage(stage);
#endif
  for (;;)
  {
#if MOUSE_WS2812B_DEMO
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - stage_started) >= stage_duration_ms[stage])
    {
      stage = (DemoStage)((stage + 1U) % DEMO_STAGE_COUNT);
      stage_started = now;
      ApplyStage(stage);
    }
#endif
    WS2812B_Update();
    osDelay(5); /* Kernel is configured for 1 kHz; no animation busy-waits. */
  }
}

bool WS2812B_TaskStart(TIM_HandleTypeDef *timer)
{
  static osThreadId_t led_thread;
  if (led_thread != NULL)
    return false;
  if (WS2812B_Init(timer) != HAL_OK)
    return false;
  const osThreadAttr_t attributes = {
    .name = "ledStrip",
    .stack_size = 1024,
    .priority = osPriorityLow
  };
  led_thread = osThreadNew(LedTask, NULL, &attributes);
  return led_thread != NULL;
}
