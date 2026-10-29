#include <string.h>
#include <limits.h>
#include "inputs.h"

void input_normalize(const input_mapping_t *map, const input_raw_t *raw,
                     locker_board_inputs_t *out)
{
    uint64_t numerator, denominator, mv;
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (map == NULL || raw == NULL) return;
    out->observed_mono_ms = raw->mono_ms;
    if (raw->gpio_valid && map->door_qualified && map->door_closed_level <= 1U && raw->door_level <= 1U)
        out->door = raw->door_level == map->door_closed_level ? LOCKER_DOOR_STATE_CLOSED : LOCKER_DOOR_STATE_OPEN;
    if (raw->gpio_valid && map->item_qualified && map->item_present_level <= 1U && raw->item_level <= 1U)
        out->item = raw->item_level == map->item_present_level ? LOCKER_ITEM_STATE_PRESENT : LOCKER_ITEM_STATE_ABSENT;
    /* Reject rails and absent calibration. A floating/disconnected two-wire
       contact cannot be diagnosed from its GPIO level alone. */
    if (!map->adc_qualified || !raw->adc_valid || raw->adc_code > 4095U ||
        map->adc_min == 0U || map->adc_max >= 4095U || map->adc_min > map->adc_max ||
        raw->adc_code < map->adc_min || raw->adc_code > map->adc_max ||
        map->reference_mv < 1800U || map->reference_mv > 3600U ||
        map->lower_ohms == 0U || map->lower_ohms > 1000000U || map->upper_ohms > 1000000U)
        return;
    numerator = (uint64_t)raw->adc_code * map->reference_mv *
                ((uint64_t)map->upper_ohms + map->lower_ohms);
    denominator = (uint64_t)4095U * map->lower_ohms;
    mv = (numerator + denominator / 2U) / denominator;
    if (mv > UINT32_MAX) return;
    out->supply_mv = (uint32_t)mv;
    out->supply_valid = true;
}

uint8_t input_debounce(input_debounce_t *state, uint8_t value,
                       uint16_t duration_ms, uint64_t now)
{
    if (state == NULL) return 0U;
    if (!state->initialized || now < state->last_ms || value == 0U) {
        memset(state, 0, sizeof(*state));
        state->initialized = true;
        state->candidate = value;
        state->since_ms = now;
    } else if (state->candidate != value) {
        state->candidate = value;
        state->since_ms = now;
    }
    state->last_ms = now;
    if (value != 0U && now - state->since_ms >= duration_ms)
        state->stable = value;
    return state->stable;
}
