#ifndef LOCK_TEST_HAL_H
#define LOCK_TEST_HAL_H
#include <stdint.h>
typedef struct { uint32_t CR1,CNT,ARR,CCR1; } TIM_TypeDef;
extern TIM_TypeDef fixture_tim4;
#define TIM4 (&fixture_tim4)
typedef struct { uint32_t Prescaler,CounterMode,Period,ClockDivision,AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t OCMode,Pulse,OCPolarity,OCFastMode; } TIM_OC_InitTypeDef;
typedef struct { uint32_t Pin,Mode,Pull,Speed,Alternate; } GPIO_InitTypeDef;
typedef struct { uint32_t APB1CLKDivider; } RCC_ClkInitTypeDef;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define GPIOB ((void *)1)
#define GPIO_PIN_6 64u
#define GPIO_PIN_RESET 0u
#define GPIO_MODE_OUTPUT_PP 1u
#define GPIO_MODE_AF_PP 2u
#define GPIO_PULLDOWN 2u
#define GPIO_SPEED_FREQ_LOW 0u
#define GPIO_AF2_TIM4 2u
#define TIM_CHANNEL_1 0u
#define TIM_CR1_CEN 1u
#define TIM_OPMODE_SINGLE 8u
#define TIM_COUNTERMODE_UP 0u
#define TIM_CLOCKDIVISION_DIV1 0u
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0u
#define TIM_OCMODE_PWM2 7u
#define TIM_OCPOLARITY_HIGH 0u
#define TIM_OCFAST_DISABLE 0u
#define TIM_FLAG_UPDATE 1u
#define RCC_HCLK_DIV1 1u
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM4_CLK_ENABLE() ((void)0)
#define __HAL_TIM_SET_COUNTER(t,n) ((t)->Instance->CNT=(n))
#define __HAL_TIM_CLEAR_FLAG(t,f) ((void)(t),(void)(f))
void HAL_GPIO_Init(void *,GPIO_InitTypeDef *);
void HAL_GPIO_WritePin(void *,unsigned,unsigned);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *,uint32_t *);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *,unsigned);
HAL_StatusTypeDef HAL_TIM_OnePulse_Init(TIM_HandleTypeDef *,unsigned);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *,TIM_OC_InitTypeDef *,unsigned);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *,unsigned);
#endif
