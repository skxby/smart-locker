#include "stm32f4xx_hal.h"
#include "lock_output.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
TIM_TypeDef fixture_tim4;
static unsigned mode,pin_level,ocmode,prescaler,fail_start;
static uint32_t pclk=42000000u;
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g)
{ assert(p==GPIOB && g->Pin==GPIO_PIN_6 && g->Pull==GPIO_PULLDOWN); mode=g->Mode; }
void HAL_GPIO_WritePin(void *p,unsigned pin,unsigned level)
{ assert(p==GPIOB && pin==GPIO_PIN_6 && level==0); pin_level=level; }
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *latency)
{ c->APB1CLKDivider=pclk==16000000u?1:4; *latency=0; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk; }
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *t,unsigned ch)
{ assert(ch==TIM_CHANNEL_1); t->Instance->CR1&=~TIM_CR1_CEN; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_OnePulse_Init(TIM_HandleTypeDef *t,unsigned opm)
{
    assert(opm==TIM_OPMODE_SINGLE); t->Instance->CR1=opm;
    t->Instance->ARR=t->Init.Period; prescaler=t->Init.Prescaler; return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *t,TIM_OC_InitTypeDef *oc,unsigned ch)
{ assert(ch==0); ocmode=oc->OCMode; t->Instance->CCR1=oc->Pulse; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *t,unsigned ch)
{ assert(ch==0 && mode==GPIO_MODE_AF_PP && t->Instance->CNT==0); if(fail_start) return HAL_ERROR; t->Instance->CR1|=1u; return HAL_OK; }
int main(void)
{
    mcu_board_profile_t p={0};
    assert(board_lock_start(1000)==LOCKER_STATUS_NOT_READY);
    board_lock_off(); assert(mode==GPIO_MODE_OUTPUT_PP && !pin_level);
    assert(board_lock_apply(NULL)==LOCKER_STATUS_NOT_READY);
    p.verified=true; p.lock_output.configured=true; p.lock_output.port_index=1;
    p.lock_output.pin_mask=GPIO_PIN_6; p.lock_output.alternate_function=GPIO_AF2_TIM4;
    p.lock_active_level=1; p.lock_timer_instance=4; p.lock_timer_channel=1; p.hal_tick_timer_instance=6; p.pulse_ms=1000;
#if BOARD_LOCK_QUALIFIED
    assert(board_lock_apply(&p)==LOCKER_STATUS_OK);
    assert(board_lock_start(0)==LOCKER_STATUS_INVALID && board_lock_start(1001)==LOCKER_STATUS_INVALID);
    assert(board_lock_start(1000)==LOCKER_STATUS_OK && board_lock_pulse_active());
    assert(board_lock_start(1)==LOCKER_STATUS_BUSY);
    assert(prescaler==8399 && fixture_tim4.ARR==10000 && fixture_tim4.CCR1==1 && ocmode==TIM_OCMODE_PWM2);
    /* Check the requested configuration in a register model; this is not a
       hardware waveform or MCU autonomous cutoff test. */
    fixture_tim4.CNT=0; fixture_tim4.CR1&=~TIM_CR1_CEN;
    assert(!board_lock_pulse_active()); pclk=16000000;
    assert(board_lock_start(100)==LOCKER_STATUS_OK && prescaler==1599 && fixture_tim4.ARR==1000);
    board_lock_off(); assert(!board_lock_pulse_active() && mode==GPIO_MODE_OUTPUT_PP);
    fail_start=1; assert(board_lock_start(100)==LOCKER_STATUS_IO_ERROR);
    assert(!board_lock_pulse_active() && mode==GPIO_MODE_OUTPUT_PP);
    p.lock_active_level=0; assert(board_lock_apply(&p)==LOCKER_STATUS_NOT_READY);
#else
    assert(board_lock_apply(&p)==LOCKER_STATUS_NOT_READY);
    assert(board_lock_start(1000)==LOCKER_STATUS_NOT_READY);
#endif
    puts("HAL configuration/qualification checks PASS (no electrical evidence)"); return 0;
}
