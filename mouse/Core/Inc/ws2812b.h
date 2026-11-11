#ifndef WS2812B_H
#define WS2812B_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* Actual strip length is not recorded in the project. Change this constant,
 * or define it in the build, to match the connected strip (1..256 LEDs).
 */
#ifndef WS2812B_LED_COUNT
#define WS2812B_LED_COUNT 8U
#endif

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
  uint8_t brightness; /* 0..100 percent, applied in every mode. */
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
 * SetColor changes the base color for STATIC/BREATHING; select the mode too.
 */
HAL_StatusTypeDef WS2812B_SetConfig(const WS2812B_Config *config);
WS2812B_Config WS2812B_GetConfig(void);
HAL_StatusTypeDef WS2812B_SetMode(WS2812B_Mode mode);
void WS2812B_SetColor(uint8_t red, uint8_t green, uint8_t blue);
HAL_StatusTypeDef WS2812B_SetBrightness(uint8_t percent);
HAL_StatusTypeDef WS2812B_SetPeriodMs(uint32_t period_ms);
bool WS2812B_IsBusy(void);
WS2812B_Status WS2812B_GetStatus(void);

/* Route the matching HAL callbacks here; these functions make no RTOS calls. */
void WS2812B_OnPulseFinished(TIM_HandleTypeDef *timer);
void WS2812B_OnTimerError(TIM_HandleTypeDef *timer);

#endif
