#ifndef LOCKER_PERIPHERALS_H
#define LOCKER_PERIPHERALS_H
#include "locker_contracts.h"
enum { BOARD_SPI_FLASH=0, BOARD_SPI_RC522=1 };
locker_status_t board_peripherals_init(void);
locker_status_t board_inputs_copy(locker_board_inputs_t *);
void board_input_task(void *);
void board_storage_task(void *);
extern volatile uint32_t s202_diag[40];
#endif
