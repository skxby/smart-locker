/* Permanent environment smoke target, not cabinet firmware. No GPIO output setup. */
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lfs.h"

volatile uint32_t env_task_progress;
volatile uint32_t env_fault;
volatile uint32_t env_littlefs_version;

void env_assert_failed(void)
{
    __disable_irq();
    env_fault = 1;
    for (;;) { __NOP(); }
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

static void environment_task(void *argument)
{
    (void)argument;
    for (;;) {
        env_task_progress++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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

int main(void)
{
    HAL_Init();
    clock_config();
    env_littlefs_version = LFS_VERSION;
    if (xTaskCreate(environment_task, "env-check", 256, NULL, 1, NULL) != pdPASS) env_assert_failed();
    vTaskStartScheduler();
    env_assert_failed();
    return 0;
}
