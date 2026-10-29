#ifndef LOCKER_COMMUNICATION_H
#define LOCKER_COMMUNICATION_H
#include <stdint.h>
void communication_task(void *argument);
/* Explicit board prerequisite; default build keeps PD5/PD6 untouched. */
#ifndef BOARD_UART_QUALIFIED
#define BOARD_UART_QUALIFIED 0
#endif
extern volatile uint32_t s203_diag[24];
#endif
