/* Restricted S2-05 empty-load waveform target. NO Journal, UART, RC522 or
 * production READY. Operator must isolate lock/MOS 12V before programming.
 * SWD mailbox is serviced only by the Control task; each command runs once. */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lock_output.h"
volatile uint32_t cutoff_command,cutoff_pulse_ms=1000u,cutoff_result,cutoff_count;
volatile uint32_t cutoff_progress,cutoff_phase,cutoff_fault;
void env_assert_failed(void)
{ board_lock_off(); cutoff_fault=1u; __disable_irq(); for(;;) __NOP(); }
void vApplicationMallocFailedHook(void) { env_assert_failed(); }
void vApplicationStackOverflowHook(TaskHandle_t t,char *n) { (void)t; (void)n; env_assert_failed(); }
void HardFault_Handler(void) { env_assert_failed(); }
void MemManage_Handler(void) { env_assert_failed(); }
void BusFault_Handler(void) { env_assert_failed(); }
void UsageFault_Handler(void) { env_assert_failed(); }
static void spin(void)
{
    /* >= SystemCoreClock/2 iterations, each requiring >=1 CPU cycle. The
       100ms test pulse must end during this interval even with IRQs masked.
       This loop duration is not used as a pulse-width measurement. */
    volatile uint32_t n;
    for(n=0;n<SystemCoreClock/2u;n++) __NOP();
}
static void control_task(void *u)
{
    mcu_board_profile_t p={0}; (void)u;
    p.verified=true; p.lock_output.configured=true; p.lock_output.port_index=1u;
    p.lock_output.pin_mask=GPIO_PIN_6; p.lock_output.alternate_function=GPIO_AF2_TIM4;
    p.lock_active_level=1u; p.lock_timer_instance=4u; p.lock_timer_channel=1u;
    p.hal_tick_timer_instance=6u; p.pulse_ms=1000u;
    cutoff_result=(uint32_t)board_lock_apply(&p);
    if(cutoff_result!=LOCKER_STATUS_OK) env_assert_failed();
    for(;;) {
        uint32_t cmd=cutoff_command;
        cutoff_command=0u;
        if(cmd>=1u && cmd<=5u) {
            uint16_t ms=(uint16_t)cutoff_pulse_ms;
            /* Modes 2/3 require a short pulse so the busy interval safely
               exceeds the cutoff under both HSI16 and HSE168 clocks. */
            if(cutoff_pulse_ms>1000u || !ms || ((cmd==2u || cmd==3u) && ms>100u)) cutoff_result=LOCKER_STATUS_INVALID;
            else {
                cutoff_phase=cmd;
                if(cmd==2u) __disable_irq();
                if(cmd==3u) vTaskSuspendAll();
                cutoff_result=(uint32_t)board_lock_start(ms);
                if(cutoff_result==LOCKER_STATUS_OK) {
                    ++cutoff_count;
                    if(cmd==2u || cmd==3u) spin();
                    else if(cmd==4u || cmd==5u) {
                        vTaskDelay(pdMS_TO_TICKS(50u));
                        if(cmd==4u) board_lock_off(); else NVIC_SystemReset();
                    }
                }
                if(cmd==2u) __enable_irq();
                if(cmd==3u) (void)xTaskResumeAll();
                cutoff_phase=0u;
            }
        } else if(cmd) cutoff_result=LOCKER_STATUS_INVALID;
        ++cutoff_progress; vTaskDelay(pdMS_TO_TICKS(10u));
    }
}
static void clock_config(void)
{
#if defined(ENV_HSE8_168)
    RCC_OscInitTypeDef osc={0}; RCC_ClkInitTypeDef clock={0};
    __HAL_RCC_PWR_CLK_ENABLE(); __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE; osc.HSEState=RCC_HSE_ON;
    osc.PLL.PLLState=RCC_PLL_ON; osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM=8; osc.PLL.PLLN=336; osc.PLL.PLLP=RCC_PLLP_DIV2; osc.PLL.PLLQ=7;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK) env_assert_failed();
    clock.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK; clock.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider=RCC_HCLK_DIV4; clock.APB2CLKDivider=RCC_HCLK_DIV2;
    if(HAL_RCC_ClockConfig(&clock,FLASH_LATENCY_5)!=HAL_OK) env_assert_failed();
#endif
    SystemCoreClockUpdate();
}
int main(void)
{
    SystemCoreClockUpdate(); board_lock_off(); HAL_Init(); clock_config(); board_lock_off();
    if(xTaskCreate(control_task,"ControlBench",512u,NULL,1u,NULL)!=pdPASS) env_assert_failed();
    vTaskStartScheduler(); env_assert_failed(); return 0;
}
