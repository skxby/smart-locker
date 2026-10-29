#ifndef LOCKER_INPUTS_H
#define LOCKER_INPUTS_H
#include "locker_contracts.h"
typedef struct {
    bool door_qualified, item_qualified, adc_qualified;
    uint8_t door_closed_level, item_present_level;
    uint32_t reference_mv, upper_ohms, lower_ohms;
    uint16_t adc_min, adc_max, debounce_ms;
} input_mapping_t;
typedef struct {
    uint8_t door_level, item_level;
    bool gpio_valid, adc_valid;
    uint16_t adc_code;
    uint64_t mono_ms;
} input_raw_t;
typedef struct {
    uint8_t candidate, stable;
    uint64_t since_ms, last_ms;
    bool initialized;
} input_debounce_t;
void input_normalize(const input_mapping_t *, const input_raw_t *, locker_board_inputs_t *);
uint8_t input_debounce(input_debounce_t *, uint8_t value, uint16_t duration_ms, uint64_t now);
#endif
