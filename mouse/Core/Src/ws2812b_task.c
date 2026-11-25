#include "ws2812b_task.h"
#include "battery_monitor.h"
#include "ws2812b.h"
#include "cmsis_os2.h"

static void LedTask(void *argument)
{
  (void)argument;
  for (;;)
  {
    BatteryMonitor_Update();
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
