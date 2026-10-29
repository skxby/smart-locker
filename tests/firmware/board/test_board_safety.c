#include <assert.h>
#include <string.h>
#include "locker_contracts.h"

int main(void)
{
    mcu_board_profile_t candidate = {0};
    locker_board_inputs_t inputs;
    assert(mcu_board_apply_profile(NULL) == LOCKER_STATUS_NOT_READY);
    assert(mcu_board_apply_profile(&candidate) == LOCKER_STATUS_NOT_READY);
    candidate.verified = true;
    assert(mcu_board_apply_profile(&candidate) == LOCKER_STATUS_NOT_READY);
    assert(mcu_board_start_bounded_pulse(0) == LOCKER_STATUS_NOT_READY);
    assert(mcu_board_start_bounded_pulse(1) == LOCKER_STATUS_NOT_READY);
    assert(mcu_board_start_bounded_pulse(65535) == LOCKER_STATUS_NOT_READY);
    memset(&inputs, 0xff, sizeof(inputs));
    assert(mcu_board_read_inputs(&inputs) == LOCKER_STATUS_NOT_READY);
    assert(inputs.door == LOCKER_DOOR_STATE_UNKNOWN);
    assert(inputs.item == LOCKER_ITEM_STATE_UNKNOWN);
    assert(!inputs.supply_valid && inputs.supply_mv == 0);
    assert(mcu_board_read_inputs(NULL) == LOCKER_STATUS_INVALID);
    assert(mcu_board_spi_transfer(0, NULL, NULL, 0, 0) == LOCKER_STATUS_NOT_READY);
    return 0;
}
