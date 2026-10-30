#ifndef MCU_CONTROL_PRIVATE_H
#define MCU_CONTROL_PRIVATE_H
#include "locker/device/codec.h"
/* All functions run on the Control owner. Assembly queues/copies producers;
 * no Storage callback may call these functions directly. Permanent objects. */
locker_status_t mcu_control_dispatch(mcu_control_t *, const mcu_request_context_t *,
                                    const locker_wire_request_union_t *);
locker_status_t mcu_control_recover(mcu_control_t *);
void mcu_control_tick(mcu_control_t *);
void mcu_control_fault(mcu_control_t *);
void mcu_control_session_lost(mcu_control_t *);
locker_status_t mcu_control_observe_card(mcu_control_t *, const uint8_t *, uint8_t);
/* Board observation, not a software timer used to switch off the output. */
bool board_lock_pulse_active(void);
#endif
