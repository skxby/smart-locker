#ifndef S203_TEST_TASK_H
#define S203_TEST_TASK_H
#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
uint32_t ulTaskNotifyTake(BaseType_t, uint32_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelay(uint32_t);
#endif
