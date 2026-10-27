/* TIM6 owns HAL milliseconds; SysTick/SVC/PendSV belong exclusively to FreeRTOS. */
#include "stm32f4xx_hal.h"

static TIM_HandleTypeDef hal_tick;

HAL_StatusTypeDef HAL_InitTick(uint32_t priority)
{
    RCC_ClkInitTypeDef clock;
    uint32_t latency, timer_clock;
    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clock, &latency);
    timer_clock = HAL_RCC_GetPCLK1Freq();
    if (clock.APB1CLKDivider != RCC_HCLK_DIV1) timer_clock *= 2U;
    hal_tick.Instance = TIM6;
    hal_tick.Init.Prescaler = timer_clock / 1000000U - 1U;
    hal_tick.Init.Period = 1000U - 1U;
    hal_tick.Init.CounterMode = TIM_COUNTERMODE_UP;
    hal_tick.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (priority >= (1UL << __NVIC_PRIO_BITS)) return HAL_ERROR;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, priority, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    uwTickPrio = priority;
    if (HAL_TIM_Base_Init(&hal_tick) != HAL_OK) return HAL_ERROR;
    return HAL_TIM_Base_Start_IT(&hal_tick);
}
void HAL_SuspendTick(void) { __HAL_TIM_DISABLE_IT(&hal_tick, TIM_IT_UPDATE); }
void HAL_ResumeTick(void) { __HAL_TIM_ENABLE_IT(&hal_tick, TIM_IT_UPDATE); }
void TIM6_DAC_IRQHandler(void) { HAL_TIM_IRQHandler(&hal_tick); }
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if (timer->Instance == TIM6) HAL_IncTick();
}
