#ifndef BOARD_LOCK_OUTPUT_H
#define BOARD_LOCK_OUTPUT_H
#include "locker_contracts.h"
/* Candidate PB6/AF2 TIM4_CH1, active high. No production approval exists yet.
 * Qualification is a local evidence-backed build input, never a UART setting. */
#ifndef BOARD_LOCK_QUALIFIED
#define BOARD_LOCK_QUALIFIED 0
#endif
#ifndef BOARD_LOCK_MAX_MS
#define BOARD_LOCK_MAX_MS 1000u
#endif
locker_status_t board_lock_apply(const mcu_board_profile_t *);
locker_status_t board_lock_start(uint16_t);
void board_lock_off(void);
bool board_lock_pulse_active(void);
#endif
