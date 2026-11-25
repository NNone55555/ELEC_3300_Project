#ifndef WS2812B_H
#define WS2812B_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* Total series length (4..256). By default: 20 profile LEDs + 3 battery LEDs. */
#ifndef WS2812B_LED_COUNT
#define WS2812B_LED_COUNT 23U
#endif
#define WS2812B_BATTERY_LED_COUNT 3U
#define WS2812B_PROFILE_LED_COUNT (WS2812B_LED_COUNT - WS2812B_BATTERY_LED_COUNT)

/* 72 MHz / 90 = 800 kHz. Defaults meet the WS2812B V5 high/low windows.
 * See WS2812B.md before changing these for a different strip revision.
 */
#ifndef WS2812B_ZERO_TICKS
#define WS2812B_ZERO_TICKS 26U
#endif
#ifndef WS2812B_ONE_TICKS
#define WS2812B_ONE_TICKS 48U
#endif

typedef enum {
  WS2812B_OFF,
  WS2812B_STATIC,
  WS2812B_BREATHING,
  WS2812B_RAINBOW
} WS2812B_Mode;

typedef struct {
  WS2812B_Mode mode;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t brightness; /* Requested 0..100 percent. Output is capped at 50%. */
  uint32_t period_ms; /* One animation cycle, 100..60000 ms; lower = faster. */
} WS2812B_Config;

typedef enum {
  WS2812B_UNINITIALIZED,
  WS2812B_RESETTING,
  WS2812B_IDLE,
  WS2812B_PREPARING,
  WS2812B_TRANSMITTING
} WS2812B_State;

typedef enum {
  WS2812B_ERROR_NONE,
  WS2812B_ERROR_HARDWARE_CONFIG,
  WS2812B_ERROR_DMA_CONFLICT,
  WS2812B_ERROR_DMA_START,
  WS2812B_ERROR_DMA_TRANSFER,
  WS2812B_ERROR_TIMEOUT
} WS2812B_Error;

typedef struct {
  WS2812B_State state;
  WS2812B_Error last_error; /* Sticky until successful reinitialization. */
  uint32_t frames_completed;
  uint32_t errors;
} WS2812B_Status;

/* Dedicated TIM1_CH1/PA8, linked to DMA1_Channel2. No heap or RTOS dependency.
 * Initialize once after CubeMX peripherals; only one task calls Update().
 */
HAL_StatusTypeDef WS2812B_Init(TIM_HandleTypeDef *timer);
void WS2812B_Update(void);

/* Task-safe settings, including one atomic profile-sized configuration copy.
 * Changes are applied asynchronously; a DMA buffer in use is never modified.
 * Profile lighting uses one LED every three, starting at the first LED.
 */
HAL_StatusTypeDef WS2812B_SetConfig(const WS2812B_Config *config);
WS2812B_Config WS2812B_GetConfig(void);
HAL_StatusTypeDef WS2812B_SetMode(WS2812B_Mode mode);
/* Last three LEDs, 10% brightness, independent of the profile configuration.
 * Zero bars clears the indicator; low battery uses red instead of green.
 */
void WS2812B_SetBatteryIndicator(uint8_t bars, bool low);
WS2812B_Status WS2812B_GetStatus(void);

/* Route the matching HAL callbacks here; these functions make no RTOS calls. */
void WS2812B_OnPulseFinished(TIM_HandleTypeDef *timer);
void WS2812B_OnTimerError(TIM_HandleTypeDef *timer);

#endif
