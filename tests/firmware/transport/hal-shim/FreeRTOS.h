#ifndef S203_TEST_FREERTOS_H
#define S203_TEST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef void *TaskHandle_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
void fixture_critical(int);
#define taskENTER_CRITICAL() fixture_critical(1)
#define taskEXIT_CRITICAL() fixture_critical(0)
#define portYIELD_FROM_ISR(wake) ((void)(wake))
#endif
