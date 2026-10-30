/* Independent S2-04 SWD diagnostic target; no UART or Control task. */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "peripherals.h"
#include "board_validation.h"

volatile uint32_t s204_fault;
void env_assert_failed(void)
{
    mcu_board_force_off(); s204_fault = 1U;
    __disable_irq(); for (;;) __NOP();
}
void vApplicationMallocFailedHook(void) { env_assert_failed(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{ (void)task; (void)name; env_assert_failed(); }
void HardFault_Handler(void) { env_assert_failed(); }
void MemManage_Handler(void) { env_assert_failed(); }
void BusFault_Handler(void) { env_assert_failed(); }
void UsageFault_Handler(void) { env_assert_failed(); }
static void clock_config(void)
{
#ifdef ENV_HSE8_168
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clock = {0};
    __HAL_RCC_PWR_CLK_ENABLE(); __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE; osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON; osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8U; osc.PLL.PLLN = 336U; osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 7U;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) env_assert_failed();
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV4; clock.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK) env_assert_failed();
#endif
    SystemCoreClockUpdate();
}
int main(void)
{
    SystemCoreClockUpdate(); HAL_Init(); clock_config(); mcu_board_force_off();
    if (board_peripherals_init() != LOCKER_STATUS_OK) env_assert_failed();
    if (xTaskCreate(s204_storage_task, "S204Storage", 2048U, NULL, 1U, NULL) != pdPASS) env_assert_failed();
    vTaskStartScheduler(); env_assert_failed(); return 0;
}
