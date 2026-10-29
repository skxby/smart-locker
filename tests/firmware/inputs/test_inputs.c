#include <assert.h>
#include <string.h>
#include "inputs.h"
#include "rc522.h"

static void test_inputs(void)
{
    input_mapping_t map = {0};
    input_raw_t raw = {0};
    locker_board_inputs_t result;
    input_debounce_t debounce = {0};
    input_normalize(&map, &raw, &result);
    assert(result.door == 0 && result.item == 0 && !result.supply_valid);
    raw.gpio_valid = true;
    map.door_qualified = map.item_qualified = true;
    map.door_closed_level = 0; map.item_present_level = 1;
    raw.door_level = 0; raw.item_level = 1; raw.mono_ms = 123;
    input_normalize(&map, &raw, &result);
    assert(result.door == LOCKER_DOOR_STATE_CLOSED && result.item == LOCKER_ITEM_STATE_PRESENT);
    assert(result.observed_mono_ms == 123);
    raw.door_level = 1; raw.item_level = 0;
    input_normalize(&map, &raw, &result);
    assert(result.door == LOCKER_DOOR_STATE_OPEN && result.item == LOCKER_ITEM_STATE_ABSENT);
    raw.gpio_valid = false;
    input_normalize(&map, &raw, &result);
    assert(result.door == 0 && result.item == 0);
    raw.adc_valid = true; raw.adc_code = 2685;
    map.adc_qualified = true; map.reference_mv = 3300;
    map.upper_ohms = 100000; map.lower_ohms = 22000; map.adc_min = 1; map.adc_max = 4094;
    input_normalize(&map, &raw, &result);
    assert(result.supply_valid && result.supply_mv >= 11998 && result.supply_mv <= 12001);
    raw.adc_code = 4095;
    input_normalize(&map, &raw, &result); assert(!result.supply_valid && result.supply_mv == 0);
    raw.adc_code = 0;
    input_normalize(&map, &raw, &result); assert(!result.supply_valid);
    raw.adc_code = 2000; map.lower_ohms = 0;
    input_normalize(&map, &raw, &result); assert(!result.supply_valid);
    map.lower_ohms = 22000; raw.adc_valid = false;
    input_normalize(&map, &raw, &result); assert(!result.supply_valid);
    input_normalize(NULL, &raw, &result); assert(result.door == 0 && !result.supply_valid);
    assert(input_debounce(&debounce, 1, 30, 0) == 0);
    assert(input_debounce(&debounce, 1, 30, 29) == 0);
    assert(input_debounce(&debounce, 2, 30, 30) == 0);
    assert(input_debounce(&debounce, 1, 30, 40) == 0);
    assert(input_debounce(&debounce, 1, 30, 70) == 1);
    assert(input_debounce(&debounce, 0, 30, 71) == 0);
    assert(input_debounce(&debounce, 2, 30, 72) == 0);
    assert(input_debounce(&debounce, 2, 30, 102) == 2);
    assert(input_debounce(&debounce, 1, 30, 1) == 0); /* Clock reversal invalidates. */
}

typedef struct {
    uint8_t registers[64], sent[64], received[64], sent_count, received_count, head;
    uint8_t uid[10], uid_length, fault;
    uint64_t now;
    bool invalid_select_crc;
} radio_t;
static uint16_t reference_crc(const uint8_t *data, unsigned length)
{
    uint16_t crc = 0x6363;
    unsigned i, bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
    }
    return crc;
}
static void reply(radio_t *m)
{
    unsigned level, offset, i;
    uint16_t crc;
    bool cascade;
    m->head = 0; m->received_count = 0;
    m->registers[4] = m->fault == 4 ? 1 : 0x30;
    m->registers[6] = m->fault == 3 ? 8 : 0;
    m->registers[0x0c] = m->fault == 5 ? 1 : 0;
    if (m->sent_count == 4 && m->sent[0] == 0x50) {
        m->registers[4] = 1; return;
    }
    if (m->sent_count == 1 && m->sent[0] == 0x52) {
        m->received[0] = 4; m->received[1] = 0; m->received_count = 2;
        return;
    }
    level = m->sent[0] == 0x93 ? 0 : m->sent[0] == 0x95 ? 1 : 2;
    offset = level * 3;
    cascade = m->uid_length > offset + 4;
    if (m->sent_count == 2 && m->sent[1] == 0x20) {
        m->received[0] = cascade ? 0x88 : m->uid[offset];
        for (i = 1; i < 4; ++i) m->received[i] = m->uid[offset + i - (cascade ? 1 : 0)];
        m->received[4] = m->received[0] ^ m->received[1] ^ m->received[2] ^ m->received[3];
        if (m->fault == 1) m->received[4] ^= 1;
        m->received_count = 5;
    } else if (m->sent_count == 9 && m->sent[1] == 0x70) {
        crc = reference_crc(m->sent, 7);
        if (m->sent[7] != (uint8_t)crc || m->sent[8] != (uint8_t)(crc >> 8)) m->invalid_select_crc = true;
        m->received[0] = cascade || m->fault == 8 ? 4 : 0;
        crc = reference_crc(m->received, 1);
        m->received[1] = (uint8_t)crc; m->received[2] = (uint8_t)(crc >> 8);
        if (m->fault == 2) m->received[1] ^= 1;
        m->received_count = 3;
    }
}
static locker_status_t radio_transfer(void *context, const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    radio_t *m = context;
    uint8_t reg = (tx[0] & 0x7e) >> 1;
    assert(length == 2);
    if (m->fault == 6) return LOCKER_STATUS_IO_ERROR;
    rx[0] = rx[1] = 0;
    if (tx[0] & 0x80) {
        rx[1] = reg == 9 ? m->received[m->head++] : reg == 0x0a ? m->received_count : m->registers[reg];
    } else if (reg == 9) {
        assert(m->sent_count < sizeof(m->sent)); m->sent[m->sent_count++] = tx[1];
    } else if (reg == 0x0a && (tx[1] & 0x80)) {
        m->sent_count = m->received_count = m->head = 0;
    } else if (reg == 4) {
        m->registers[4] = 0;
    } else {
        m->registers[reg] = reg == 1 && tx[1] == 0x0f ? 0 : tx[1];
        if (reg == 0x0d && (tx[1] & 0x80)) reply(m);
    }
    return LOCKER_STATUS_OK;
}
static uint64_t radio_now(void *context) { return ((radio_t *)context)->now; }
static void radio_wait(void *context, uint32_t ms) { ((radio_t *)context)->now += ms; }
static void test_uid(void)
{
    unsigned length, fault, i;
    for (length = 4; length <= 10; length += 3) {
        radio_t m = {0};
        rc522_t r = {0};
        struct { rc522_uid_t uid; uint32_t guard; } output;
        m.uid_length = (uint8_t)length; m.registers[0x37] = length == 7 ? 0x82 : 0x92;
        for (i = 0; i < length; ++i) m.uid[i] = (uint8_t)(i + 0x10);
        r.context = &m; r.transfer = radio_transfer; r.now_ms = radio_now; r.wait_ms = radio_wait;
        assert(rc522_init(&r) == LOCKER_STATUS_OK);
        output.guard = 0x12345678;
        assert(rc522_read_uid(&r, &output.uid) == LOCKER_STATUS_OK);
        assert(output.uid.length == length && memcmp(output.uid.bytes, m.uid, length) == 0);
        assert(output.guard == 0x12345678 && !m.invalid_select_crc);
        for (fault = 1; fault <= 8; ++fault) {
            if (fault == 7) continue;
            m.fault = (uint8_t)fault;
            memset(&output.uid, 0xff, sizeof(output.uid));
            assert(rc522_read_uid(&r, &output.uid) != LOCKER_STATUS_OK);
            assert(output.uid.length == 0 && output.guard == 0x12345678);
            for (i = 0; i < 10; ++i) assert(output.uid.bytes[i] == 0);
        }
        m.fault = 0; m.registers[0x37] = 0;
        assert(rc522_init(&r) == LOCKER_STATUS_NOT_READY && !r.ready);
    }
    assert(rc522_crc_a((const uint8_t *)"abc", 3) == reference_crc((const uint8_t *)"abc", 3));
}
int main(void) { test_inputs(); test_uid(); return 0; }
