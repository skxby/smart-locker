#include "../../transport/hal-shim/stm32f4xx_hal.h"
static inline unsigned __get_IPSR(void) { return 0; }
#define GPIOE ((void *)5)
#define GPIO_PIN_3 8u
#define GPIO_PIN_RESET 0u
#define GPIO_PIN_SET 1u
void HAL_GPIO_WritePin(void *,unsigned,unsigned);
