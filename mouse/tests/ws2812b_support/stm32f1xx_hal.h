#ifndef WS2812B_TEST_HAL_H
#define WS2812B_TEST_HAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET, GPIO_PIN_SET} GPIO_PinState;
typedef enum {HAL_TIM_CHANNEL_STATE_READY, HAL_TIM_CHANNEL_STATE_BUSY} HAL_TIM_ChannelStateTypeDef;
typedef struct {uint32_t CR1, CR2, PSC, ARR, CNT, CCR1, EGR, SR, DIER, CCMR1, CCER, BDTR;} TIM_TypeDef;
typedef struct {uint32_t CR2;} SPI_TypeDef;
typedef struct {uint32_t MAPR;} AFIO_TypeDef;
typedef struct {uint32_t CCR, CNDTR;} DMA_Channel_TypeDef;
typedef struct {uint32_t levels, mode;} GPIO_TypeDef;
typedef struct {uint32_t APB2CLKDivider;} RCC_ClkInitTypeDef;
typedef struct {uint32_t Pin, Mode, Pull, Speed;} GPIO_InitTypeDef;
typedef struct {uint32_t Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode;} DMA_InitTypeDef;
typedef struct {uint32_t Prescaler, Period, CounterMode, RepetitionCounter;} TIM_Base_InitTypeDef;
typedef struct {
  DMA_Channel_TypeDef *Instance;
  DMA_InitTypeDef Init;
  void *Parent;
  uint32_t State;
  uint32_t interrupts;
} DMA_HandleTypeDef;
typedef struct {
  TIM_TypeDef *Instance;
  TIM_Base_InitTypeDef Init;
  DMA_HandleTypeDef *hdma[7];
  uint32_t Channel;
  HAL_TIM_ChannelStateTypeDef channel_state;
} TIM_HandleTypeDef;

extern TIM_TypeDef test_tim1;
extern SPI_TypeDef test_spi1;
extern AFIO_TypeDef test_afio;
extern GPIO_TypeDef test_gpioa;
extern DMA_Channel_TypeDef test_dma2;
extern uint32_t test_primask;
#define TIM1 (&test_tim1)
#define SPI1 (&test_spi1)
#define AFIO (&test_afio)
#define GPIOA (&test_gpioa)
#define DMA1_Channel2 (&test_dma2)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 3U
#define RCC_HCLK_DIV1 0U
#define RCC_HCLK_DIV2 1U
#define TIM_CHANNEL_1 0U
#define TIM_COUNTERMODE_UP 0U
#define TIM_DMA_ID_CC1 1U
#define TIM_DMA_CC1 (1U << 9)
#define TIM_CR1_CEN 1U
#define TIM_CR1_DIR (1U << 4)
#define TIM_CR1_CMS (3U << 5)
#define TIM_CR2_CCDS (1U << 3)
#define TIM_CCMR1_CC1S 3U
#define TIM_CCMR1_OC1M (7U << 4)
#define TIM_CCMR1_OC1PE (1U << 3)
#define TIM_OCMODE_PWM1 (6U << 4)
#define TIM_CCER_CC1P (1U << 1)
#define TIM_CCER_CCxE_MASK 0x1111U
#define TIM_CCER_CCxNE_MASK 0x0444U
#define AFIO_MAPR_TIM1_REMAP (3U << 6)
#define TIM_EGR_UG 1U
#define TIM_FLAG_UPDATE 1U
#define TIM_FLAG_CC1 2U
#define HAL_TIM_ACTIVE_CHANNEL_1 1U
#define HAL_TIM_ACTIVE_CHANNEL_2 2U
#define DMA_MEMORY_TO_PERIPH 1U
#define DMA_NORMAL 0U
#define DMA_MINC_ENABLE 1U
#define DMA_PINC_DISABLE 0U
#define DMA_MDATAALIGN_HALFWORD 2U
#define DMA_PDATAALIGN_HALFWORD 2U
#define HAL_DMA_STATE_READY 1U
#define HAL_DMA_STATE_BUSY 2U
#define DMA_IT_HT 2U
#define SPI_CR2_RXDMAEN 1U

static inline uint32_t __get_PRIMASK(void) {return test_primask;}
static inline void __disable_irq(void) {test_primask = 1U;}
static inline void __set_PRIMASK(uint32_t value) {test_primask = value;}
#define __HAL_TIM_DISABLE_DMA(h, bit) ((h)->Instance->DIER &= ~(bit))
#define __HAL_TIM_SET_COMPARE(h, ch, value) ((void)(ch), (h)->Instance->CCR1 = (value))
#define __HAL_TIM_SET_COUNTER(h, value) ((h)->Instance->CNT = (value))
#define __HAL_TIM_CLEAR_FLAG(h, flags) ((h)->Instance->SR &= ~(flags))
#define __HAL_DMA_DISABLE(h) ((h)->Instance->CCR &= ~1U)
#define __HAL_DMA_DISABLE_IT(h, flags) ((h)->interrupts &= ~(flags))

uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *config, uint32_t *latency);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pin, GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config);
HAL_TIM_ChannelStateTypeDef HAL_TIM_GetChannelState(const TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *timer, uint32_t channel,
                                       const uint32_t *data, uint16_t size);
HAL_StatusTypeDef HAL_TIM_PWM_Stop_DMA(TIM_HandleTypeDef *timer, uint32_t channel);

#endif
