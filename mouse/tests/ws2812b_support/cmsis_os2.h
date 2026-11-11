#ifndef WS2812B_TEST_CMSIS_H
#define WS2812B_TEST_CMSIS_H

#include <stdint.h>
typedef void *osThreadId_t;
typedef struct {const char *name; uint32_t stack_size; int priority;} osThreadAttr_t;
#define osPriorityLow 8
osThreadId_t osThreadNew(void (*function)(void *), void *argument, const osThreadAttr_t *attributes);
void osDelay(uint32_t milliseconds);

#endif
