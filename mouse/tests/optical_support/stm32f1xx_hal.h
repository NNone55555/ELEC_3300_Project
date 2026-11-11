#ifndef MOCK_HAL_H
#define MOCK_HAL_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {int unused;} SPI_HandleTypeDef;
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
typedef struct {uint32_t CTRL,CYCCNT;} DWT_Type;
typedef struct {uint32_t DEMCR;} Debug_Type;
extern Debug_Type debug;
DWT_Type *MockDWT(void);
void MockNop(void);
#define DWT MockDWT()
#define CoreDebug (&debug)
#define CoreDebug_DEMCR_TRCENA_Msk 0x1000000U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define __NOP() MockNop()
uint32_t HAL_RCC_GetHCLKFreq(void);
void HAL_GPIO_WritePin(void*,uint16_t,GPIO_PinState);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef*,uint8_t*,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef*,uint8_t*,uint8_t*,uint16_t,uint32_t);
#endif
