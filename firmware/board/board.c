/* S2-01 BSP boundary: an unverified board never energizes the lock. */
#include <string.h>
#include "locker_contracts.h"

locker_status_t mcu_board_apply_profile(const mcu_board_profile_t *profile)
{
    /* No production profile is verified yet. A caller supplied profile cannot
       turn a candidate wiring diagram into a hardware approval. */
    if (profile == NULL || !profile->verified) return LOCKER_STATUS_NOT_READY;
    return LOCKER_STATUS_NOT_READY;
}

void mcu_board_force_off(void)
{
    /* The lock GPIO is deliberately not configured on an unverified board. */
}

locker_status_t mcu_board_start_bounded_pulse(uint16_t pulse_ms)
{
    (void)pulse_ms;
    return LOCKER_STATUS_NOT_READY;
}

locker_status_t mcu_board_read_inputs(locker_board_inputs_t *result)
{
    if (result == NULL) return LOCKER_STATUS_INVALID;
    memset(result, 0, sizeof(*result));
    result->door = LOCKER_DOOR_STATE_UNKNOWN;
    result->item = LOCKER_ITEM_STATE_UNKNOWN;
    return LOCKER_STATUS_NOT_READY;
}

locker_status_t mcu_board_spi_transfer(uint8_t chip_select, const uint8_t *tx,
                                      uint8_t *rx, uint16_t length, uint32_t timeout_us)
{
    (void)chip_select; (void)tx; (void)rx; (void)length; (void)timeout_us;
    return LOCKER_STATUS_NOT_READY;
}

locker_status_t mcu_board_set_alarm(locker_alarm_pattern_t pattern)
{
    (void)pattern;
    return LOCKER_STATUS_NOT_READY;
}
