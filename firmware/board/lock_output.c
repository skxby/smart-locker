/* Hardware OPM stops TIM4 on update and PWM2 returns low at CNT=0.
 * No IRQ, RTOS timer, filesystem completion, or host packet performs cutoff. */
#include "stm32f4xx_hal.h"
#include "lock_output.h"
static TIM_HandleTypeDef timer;
static bool ready;

void board_lock_off(void)
{
    GPIO_InitTypeDef pin={0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* Preload low before handing the pin back from timer AF to GPIO. */
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);
    pin.Pin=GPIO_PIN_6; pin.Mode=GPIO_MODE_OUTPUT_PP;
    pin.Pull=GPIO_PULLDOWN; pin.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB,&pin);
    if(timer.Instance==TIM4) (void)HAL_TIM_PWM_Stop(&timer,TIM_CHANNEL_1);
}
bool board_lock_pulse_active(void)
{ return ready && timer.Instance==TIM4 && (timer.Instance->CR1 & TIM_CR1_CEN)!=0u; }
locker_status_t board_lock_apply(const mcu_board_profile_t *p)
{
    board_lock_off(); ready=false;
    if(!BOARD_LOCK_QUALIFIED || BOARD_LOCK_MAX_MS==0u || BOARD_LOCK_MAX_MS>1000u ||
       !p || !p->verified || !p->lock_output.configured || p->lock_output.port_index!=1u ||
       p->lock_output.pin_mask!=GPIO_PIN_6 || p->lock_output.alternate_function!=GPIO_AF2_TIM4 ||
       p->lock_active_level!=1u || p->lock_timer_instance!=4u || p->lock_timer_channel!=1u ||
       p->hal_tick_timer_instance!=6u || p->pulse_ms==0u || p->pulse_ms>BOARD_LOCK_MAX_MS)
        return LOCKER_STATUS_NOT_READY;
    ready=true; return LOCKER_STATUS_OK;
}
locker_status_t board_lock_start(uint16_t ms)
{
    RCC_ClkInitTypeDef clocks;
    TIM_OC_InitTypeDef output={0};
    GPIO_InitTypeDef pin={0};
    uint32_t latency,hz;
    if(!ready) return LOCKER_STATUS_NOT_READY;
    if(ms==0u || ms>BOARD_LOCK_MAX_MS || ms>1000u) return LOCKER_STATUS_INVALID;
    if(board_lock_pulse_active()) return LOCKER_STATUS_BUSY;
    board_lock_off();
    HAL_RCC_GetClockConfig(&clocks,&latency);
    hz=HAL_RCC_GetPCLK1Freq(); if(clocks.APB1CLKDivider!=RCC_HCLK_DIV1) hz*=2u;
    if(hz<10000u || hz%10000u!=0u) return LOCKER_STATUS_NOT_READY;
    __HAL_RCC_TIM4_CLK_ENABLE();
    timer.Instance=TIM4; timer.Init.Prescaler=hz/10000u-1u;
    timer.Init.CounterMode=TIM_COUNTERMODE_UP;
    /* One low tick, followed by exactly ms*10 high ticks, then low forever.
       PWM1 would be high again at the stopped CNT=0 and is unsafe here. */
    timer.Init.Period=(uint32_t)ms*10u;
    timer.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    timer.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_DISABLE;
    if(HAL_TIM_OnePulse_Init(&timer,TIM_OPMODE_SINGLE)!=HAL_OK) goto failed;
    output.OCMode=TIM_OCMODE_PWM2; output.Pulse=1u;
    output.OCPolarity=TIM_OCPOLARITY_HIGH; output.OCFastMode=TIM_OCFAST_DISABLE;
    if(HAL_TIM_PWM_ConfigChannel(&timer,&output,TIM_CHANNEL_1)!=HAL_OK) goto failed;
    __HAL_TIM_SET_COUNTER(&timer,0u);
    __HAL_TIM_CLEAR_FLAG(&timer,TIM_FLAG_UPDATE);
    /* Configure AF only while the timer output is low at CNT=0. */
    pin.Pin=GPIO_PIN_6; pin.Mode=GPIO_MODE_AF_PP; pin.Pull=GPIO_PULLDOWN;
    pin.Speed=GPIO_SPEED_FREQ_LOW; pin.Alternate=GPIO_AF2_TIM4; HAL_GPIO_Init(GPIOB,&pin);
    if(HAL_TIM_PWM_Start(&timer,TIM_CHANNEL_1)!=HAL_OK) goto failed;
    return LOCKER_STATUS_OK;
failed:
    board_lock_off(); ready=false; return LOCKER_STATUS_IO_ERROR;
}
