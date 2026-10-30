/* S2-01 safe application entry. Functional tasks are added by later cards. */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "locker_contracts.h"
#ifdef LOCKER_S205
#include "assembly.h"
#endif
#ifdef LOCKER_S203
#include "communication.h"
#endif
#ifdef LOCKER_S202
#include "peripherals.h"
#endif

volatile uint32_t s201_fault;
/* Persistent startup diagnostics, readable through SWD. Bits 0..3 mean
   NULL/unverified/claimed-verified profiles and pulse were all rejected. */
volatile uint32_t s201_safety_checks;

void env_assert_failed(void)
{
    mcu_board_force_off();
    s201_fault = 1U;
#ifdef LOCKER_S202
    s202_diag[31] = 1U;
#endif
    __disable_irq();
    for (;;) { __NOP(); }
}

void vApplicationMallocFailedHook(void) { env_assert_failed(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name;
    env_assert_failed();
}
void HardFault_Handler(void) { env_assert_failed(); }
void MemManage_Handler(void) { env_assert_failed(); }
void BusFault_Handler(void) { env_assert_failed(); }
void UsageFault_Handler(void) { env_assert_failed(); }

static void inactive_role(void *argument)
{
    (void)argument;
    for (;;) vTaskDelay(portMAX_DELAY);
}

static void clock_config(void)
{
#if defined(ENV_HSE8_168)
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clock = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) env_assert_failed();
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV4;
    clock.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK) env_assert_failed();
#endif
    SystemCoreClockUpdate();
}

int main(void)
{
    static const char *const names[4] = {"Control", "Communication", "Storage", "Health"};
    uint32_t i;
    SystemCoreClockUpdate();
    HAL_Init();
    clock_config();
    mcu_board_force_off();
#ifndef LOCKER_S205
    {
        mcu_board_profile_t candidate = {0};
        if (mcu_board_apply_profile(NULL) == LOCKER_STATUS_NOT_READY)
            s201_safety_checks |= 1U;
        if (mcu_board_apply_profile(&candidate) == LOCKER_STATUS_NOT_READY)
            s201_safety_checks |= 2U;
        candidate.verified = true;
        if (mcu_board_apply_profile(&candidate) == LOCKER_STATUS_NOT_READY)
            s201_safety_checks |= 4U;
        if (mcu_board_start_bounded_pulse(1U) == LOCKER_STATUS_NOT_READY)
            s201_safety_checks |= 8U;
        if (s201_safety_checks != 15U) env_assert_failed();
    }
#endif
#ifdef LOCKER_S202
    if (board_peripherals_init() != LOCKER_STATUS_OK) env_assert_failed();
#endif
#ifdef LOCKER_S205
    (void)names; (void)i;
    assembly_start();
#else
    for (i = 0U; i < 4U; ++i) {
#ifdef LOCKER_S202
        TaskFunction_t role = i == 0U ? board_input_task : i == 2U ? board_storage_task : inactive_role;
#ifdef LOCKER_S203
        if (i == 1U) role = communication_task;
#endif
#ifdef LOCKER_S203
        if (xTaskCreate(role, names[i], i == 1U ? 1024U : 768U, NULL, i == 1U ? 2U : 1U, NULL) != pdPASS)
#else
        if (xTaskCreate(role, names[i], 768U, NULL, 1U, NULL) != pdPASS)
#endif
#else
        if (xTaskCreate(inactive_role, names[i], 256U, NULL, 1U, NULL) != pdPASS)
#endif
            env_assert_failed();
    }
#endif
    vTaskStartScheduler();
    env_assert_failed();
    return 0;
}
