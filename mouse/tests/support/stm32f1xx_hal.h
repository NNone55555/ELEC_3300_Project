#ifndef TEST_STM32_HAL_H
#define TEST_STM32_HAL_H

#include <stdint.h>
#include <stddef.h>

/* Host-only GPIO/interrupt model. Firmware uses the actual ST HAL. */
typedef struct { uint16_t levels; } GPIO_TypeDef;
typedef struct { unsigned unused; } TIM_HandleTypeDef;
typedef struct { unsigned unused; } SPI_HandleTypeDef;
typedef struct { volatile uint32_t CR1, SR2; } I2C_TypeDef;
typedef struct { I2C_TypeDef *Instance; } I2C_HandleTypeDef;
typedef struct { volatile uint32_t AHBENR; } RCC_TypeDef;
extern RCC_TypeDef test_rcc;
#define RCC (&test_rcc)
#define RCC_AHBENR_FSMCEN (1U << 8)
#define I2C_CR1_STOP (1U << 9)
#define I2C_SR2_BUSY (1U << 1)
#define __HAL_RCC_FSMC_CLK_DISABLE() (RCC->AHBENR &= ~RCC_AHBENR_FSMCEN)
#define __HAL_RCC_FSMC_CLK_ENABLE() (RCC->AHBENR |= RCC_AHBENR_FSMCEN)
void Test_DataBarrier(void);
#define __DSB() Test_DataBarrier()
typedef struct { uint32_t RxState; } UART_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_RX 1U
#define I2C_MEMADD_SIZE_8BIT 1U
#define __HAL_UART_CLEAR_OREFLAG(h) ((void)(h))
#define __DMB() ((void)0)
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, const uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *, uint16_t, uint32_t, uint32_t);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *);
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef test_gpio[5];
extern uint32_t test_primask;
#define GPIOA (&test_gpio[0])
#define GPIOB (&test_gpio[1])
#define GPIOC (&test_gpio[2])
#define GPIOD (&test_gpio[3])
#define GPIOE (&test_gpio[4])
#define GPIO_PIN_0 (1U << 0)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_2 (1U << 2)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_7 (1U << 7)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define UNUSED(value) ((void)(value))
static inline uint32_t __get_PRIMASK(void) { return test_primask; }
static inline void __disable_irq(void) { test_primask = 1U; }
static inline void __set_PRIMASK(uint32_t value) { test_primask = value; }
static inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ return (port->levels & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET; }
static inline uint32_t HAL_GetUIDw0(void) { return 0x12345678U; }
static inline uint32_t HAL_GetUIDw1(void) { return 0x9ABCDEF0U; }
static inline uint32_t HAL_GetUIDw2(void) { return 0xFEDCBA98U; }
uint32_t HAL_GetTick(void);

#endif
