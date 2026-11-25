#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* Calibrate after MX_ADC1_Init. The LED task alone services the ADC afterward. */
bool BatteryMonitor_Init(ADC_HandleTypeDef *adc);
void BatteryMonitor_Update(void);

/* Latest raw conversion (0..4095), or -1 when no reading is available. */
int32_t BatteryMonitor_GetRaw(void);

#endif
