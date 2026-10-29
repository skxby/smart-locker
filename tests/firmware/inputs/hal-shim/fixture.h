/* Host-only HAL/RTOS fixture for the real BSP transaction function.
   Never included in firmware targets; not hardware acceptance evidence. */
#ifndef TEST_PERIPHERAL_FIXTURE_H
#define TEST_PERIPHERAL_FIXTURE_H
#include <stdint.h>
#include <pthread.h>
typedef enum { HAL_OK=0, HAL_ERROR=1, HAL_BUSY=2, HAL_TIMEOUT=3 } HAL_StatusTypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { unsigned index; } GPIO_TypeDef;
extern GPIO_TypeDef fixture_gpio[4];
#define GPIOA (&fixture_gpio[0])
#define GPIOB (&fixture_gpio[1])
#define GPIOE (&fixture_gpio[2])
#define GPIO_PIN_0 1U
#define GPIO_PIN_1 2U
#define GPIO_PIN_3 8U
#define GPIO_PIN_4 16U
#define GPIO_PIN_5 32U
#define GPIO_PIN_6 64U
#define GPIO_PIN_7 128U
#define GPIO_PIN_12 4096U
#define GPIO_PIN_13 8192U
#define GPIO_PIN_14 16384U
#define GPIO_PIN_15 32768U
#define GPIO_PIN_SET 1U
#define GPIO_PIN_RESET 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_MODE_INPUT 0U
#define GPIO_NOPULL 0U
#define GPIO_PULLUP 1U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_SPEED_FREQ_MEDIUM 1U
#define GPIO_AF5_SPI1 5U
#define GPIO_AF5_SPI2 5U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SPI1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SPI2_CLK_ENABLE() ((void)0)
typedef struct {
 void *Instance;
 struct { unsigned Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS,
 BaudRatePrescaler, FirstBit, TIMode, CRCCalculation; } Init;
} SPI_HandleTypeDef;
typedef struct { unsigned unused; } ADC_HandleTypeDef;
#define SPI1 ((void *)1)
#define SPI2 ((void *)2)
#define SPI_MODE_MASTER 1U
#define SPI_DIRECTION_2LINES 0U
#define SPI_DATASIZE_8BIT 8U
#define SPI_POLARITY_LOW 0U
#define SPI_PHASE_1EDGE 0U
#define SPI_NSS_SOFT 1U
#define SPI_BAUDRATEPRESCALER_128 128U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_TIMODE_DISABLE 0U
#define SPI_CRCCALCULATION_DISABLE 0U
typedef pthread_mutex_t *SemaphoreHandle_t;
#define pdTRUE 1
#define taskSCHEDULER_RUNNING 1
#define pdMS_TO_TICKS(x) (x)
extern pthread_mutex_t fixture_critical;
#define taskENTER_CRITICAL() pthread_mutex_lock(&fixture_critical)
#define taskEXIT_CRITICAL() pthread_mutex_unlock(&fixture_critical)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskGetSchedulerState(void);
void vTaskDelay(unsigned);
uint32_t __get_IPSR(void);
uint32_t HAL_GetTick(void);
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint32_t, unsigned);
unsigned HAL_GPIO_ReadPin(GPIO_TypeDef *, uint32_t);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *, uint8_t *, uint8_t *, uint16_t, uint32_t);
#endif
