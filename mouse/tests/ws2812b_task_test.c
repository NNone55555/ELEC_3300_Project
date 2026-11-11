#include "ws2812b_task.h"
#include "ws2812b.h"
#include "cmsis_os2.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick;
static unsigned updates, creations, stage_count;
static uint32_t deadline;
static void (*task_function)(void *);
static bool fail_init = true;
static jmp_buf escape;

uint32_t HAL_GetTick(void) {return tick;}
HAL_StatusTypeDef WS2812B_Init(TIM_HandleTypeDef *timer)
{assert(timer != NULL); return fail_init ? HAL_ERROR : HAL_OK;}
void WS2812B_Update(void) {++updates;}
HAL_StatusTypeDef WS2812B_SetConfig(const WS2812B_Config *config)
{
  const WS2812B_Mode modes[] = {WS2812B_STATIC,WS2812B_STATIC,WS2812B_STATIC,WS2812B_STATIC,
                               WS2812B_OFF,WS2812B_BREATHING,WS2812B_RAINBOW,WS2812B_STATIC};
  const uint8_t colors[][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255},
                               {0,0,0},{0,0,255},{0,0,0},{255,0,0}};
  assert(stage_count < 8U && config->mode == modes[stage_count]);
  assert(config->red == colors[stage_count][0] && config->green == colors[stage_count][1]
         && config->blue == colors[stage_count][2] && config->brightness == 20U);
  assert(config->period_ms == (stage_count == 6U ? 4000U : 3000U));
  const uint32_t times[] = {0,2000,4000,6000,8000,9000,15000,23000};
  assert(tick == times[stage_count]);
  ++stage_count;
  return HAL_OK;
}
osThreadId_t osThreadNew(void (*function)(void *), void *argument, const osThreadAttr_t *attributes)
{
  assert(argument == NULL && strcmp(attributes->name, "ledStrip") == 0);
  assert(attributes->priority == osPriorityLow && attributes->stack_size == 1024);
  task_function = function; ++creations;
  return (void *)1;
}
void osDelay(uint32_t milliseconds)
{
  assert(milliseconds == 5U);
  tick += milliseconds;
  if (tick >= deadline) longjmp(escape, 1);
}
int main(void)
{
  TIM_HandleTypeDef timer = {0};
  assert(!WS2812B_TaskStart(&timer) && creations == 0U);
  fail_init = false;
  assert(WS2812B_TaskStart(&timer) && creations == 1U && task_function != NULL);
  assert(!WS2812B_TaskStart(&timer));
#if MOUSE_WS2812B_DEMO
  deadline = 23005U;
#else
  deadline = 100U;
#endif
  if (setjmp(escape) == 0) task_function(NULL);
  assert(updates == deadline / 5U);
#if MOUSE_WS2812B_DEMO
  assert(stage_count == 8U);
  puts("PASS: independent FreeRTOS LED task, bounded 5 ms yields, all seven demo stages and repeat.");
#else
  assert(stage_count == 0U);
  puts("PASS: demo disabled leaves settings untouched while the LED service task keeps running.");
#endif
  return 0;
}
