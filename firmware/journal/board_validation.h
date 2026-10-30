#ifndef S204_BOARD_VALIDATION_H
#define S204_BOARD_VALIDATION_H
#include "locker_contracts.h"
extern volatile uint32_t s204_diag[32];
extern volatile uint8_t s204_data[8192];
extern volatile uint32_t s204_command, s204_address, s204_length;
extern mcu_journal_state_t s204_state;
extern locker_event_batch_t s204_page;
void s204_storage_task(void *argument);
#endif
