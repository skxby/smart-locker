/* ISO14443A cascade selection, cross-checked with miguelbalboa/rfid
   (public domain/Unlicense). No Arduino runtime or authentication operations. */
#include <string.h>
#include "rc522.h"
enum { COMMAND=0x01, IRQ=0x04, ERROR=0x06, FIFO=0x09, FIFO_LEVEL=0x0a,
       CONTROL=0x0c, FRAMING=0x0d, COLL=0x0e, TX_CONTROL=0x14, VERSION=0x37 };
uint16_t rc522_crc_a(const uint8_t *bytes, uint8_t length)
{
    uint16_t crc = 0x6363U;
    uint8_t i, data;
    for (i = 0U; i < length; ++i) {
        data = bytes[i] ^ (uint8_t)crc;
        data ^= (uint8_t)(data << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)data << 8) ^ ((uint16_t)data << 3) ^ (data >> 4));
    }
    return crc;
}
static locker_status_t reg_write(rc522_t *r, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(reg << 1), value}, rx[2];
    return r->transfer(r->context, tx, rx, 2U);
}
static locker_status_t reg_read(rc522_t *r, uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = {(uint8_t)(0x80U | (reg << 1)), 0U}, rx[2] = {0};
    locker_status_t status = r->transfer(r->context, tx, rx, 2U);
    if (status == LOCKER_STATUS_OK) *value = rx[1];
    return status;
}
#define TRY(call) do { locker_status_t s_ = (call); if (s_ != LOCKER_STATUS_OK) return s_; } while (0)
locker_status_t rc522_init(rc522_t *r)
{
    static const uint8_t setup[][2] = {{0x2a,0x80},{0x2b,0xa9},{0x2c,0x03},
        {0x2d,0xe8},{0x15,0x40},{0x11,0x3d},{0x12,0x00},{0x13,0x00}};
    uint8_t i, value = 0U;
    if (r == NULL || r->transfer == NULL || r->now_ms == NULL || r->wait_ms == NULL)
        return LOCKER_STATUS_INVALID;
    r->ready = false;
    r->version = 0U;
    TRY(reg_write(r, COMMAND, 0x0f));
    for (i = 0U; i < 50U; ++i) {
        r->wait_ms(r->context, 1U);
        TRY(reg_read(r, COMMAND, &value));
        if (!(value & 0x10U)) break;
    }
    if (value & 0x10U) return LOCKER_STATUS_NOT_READY;
    TRY(reg_read(r, VERSION, &r->version));
    /* 0x82 is an observed compatible module revision, not NXP identity.
       Require all configuration readbacks and actual UID/BCC/CRC validation. */
    if (r->version != 0x91U && r->version != 0x92U && r->version != 0x82U)
        return LOCKER_STATUS_NOT_READY;
    for (i = 0U; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        TRY(reg_write(r, setup[i][0], setup[i][1]));
        TRY(reg_read(r, setup[i][0], &value));
        if (value != setup[i][1]) return LOCKER_STATUS_IO_ERROR;
    }
    TRY(reg_read(r, TX_CONTROL, &value));
    TRY(reg_write(r, TX_CONTROL, value | 3U));
    TRY(reg_read(r, TX_CONTROL, &value));
    if ((value & 3U) != 3U) return LOCKER_STATUS_IO_ERROR;
    r->ready = true;
    return LOCKER_STATUS_OK;
}
static locker_status_t exchange(rc522_t *r, const uint8_t *tx, uint8_t length,
                                 uint8_t bits, uint8_t *rx, uint8_t wanted)
{
    uint8_t i, irq = 0U, error = 0U, count = 0U, valid_bits = 0U;
    uint64_t start = r->now_ms(r->context);
    locker_status_t status;
    r->last_irq = r->last_error = r->last_count = r->last_bits = 0U;
    r->last_command = tx[0];
    TRY(reg_write(r, COMMAND, 0U));
    TRY(reg_write(r, IRQ, 0x7fU));
    TRY(reg_write(r, FIFO_LEVEL, 0x80U));
    TRY(reg_write(r, FRAMING, bits));
    for (i = 0U; i < length; ++i) TRY(reg_write(r, FIFO, tx[i]));
    TRY(reg_write(r, COMMAND, 0x0cU));
    TRY(reg_write(r, FRAMING, bits | 0x80U));
    for (i = 0U; i < 40U; ++i) {
        status = reg_read(r, IRQ, &irq);
        if (status != LOCKER_STATUS_OK) break;
        if (irq & 0x31U) break;
        if (r->now_ms(r->context) - start >= 35U) break;
        r->wait_ms(r->context, 1U);
    }
    /* Always leave StartSend clear, including timer/software timeout. */
    {
        locker_status_t cleanup = reg_write(r, FRAMING, bits);
        r->last_irq = irq;
        if (status != LOCKER_STATUS_OK) return status;
        if (cleanup != LOCKER_STATUS_OK) return cleanup;
    }
    if (!(irq & 0x30U)) return LOCKER_STATUS_NOT_READY;
    TRY(reg_read(r, ERROR, &error));
    r->last_error = error;
    if (error & 0x08U) return LOCKER_STATUS_CONFLICT; /* Multiple cards: reject. */
    if (error & 0xd7U) return LOCKER_STATUS_IO_ERROR;
    TRY(reg_read(r, FIFO_LEVEL, &count));
    TRY(reg_read(r, CONTROL, &valid_bits));
    r->last_count = count; r->last_bits = valid_bits & 7U;
    if (count != wanted || (valid_bits & 7U)) return LOCKER_STATUS_IO_ERROR;
    for (i = 0U; i < count; ++i) TRY(reg_read(r, FIFO, &rx[i]));
    return LOCKER_STATUS_OK;
}
locker_status_t rc522_read_uid(rc522_t *r, rc522_uid_t *uid)
{
    static const uint8_t selectors[3] = {0x93U, 0x95U, 0x97U};
    rc522_uid_t result = {0};
    uint8_t request = 0x52U, atqa[2], block[5], sak[3], frame[9];
    uint8_t level, i, check, ct;
    uint16_t crc;
    if (uid == NULL) return LOCKER_STATUS_INVALID;
    memset(uid, 0, sizeof(*uid));
    if (r == NULL || !r->ready || r->transfer == NULL || r->now_ms == NULL || r->wait_ms == NULL)
        return LOCKER_STATUS_NOT_READY;
    TRY(exchange(r, &request, 1U, 7U, atqa, 2U));
    for (level = 0U; level < 3U; ++level) {
        TRY(reg_write(r, COLL, 0U));
        frame[0] = selectors[level]; frame[1] = 0x20U;
        TRY(exchange(r, frame, 2U, 0U, block, 5U));
        check = 0U;
        for (i = 0U; i < 4U; ++i) check ^= block[i];
        if (check != block[4]) return LOCKER_STATUS_IO_ERROR;
        ct = block[0] == 0x88U;
        if (level == 2U && ct) return LOCKER_STATUS_IO_ERROR;
        frame[1] = 0x70U;
        memcpy(&frame[2], block, 5U);
        crc = rc522_crc_a(frame, 7U);
        frame[7] = (uint8_t)crc; frame[8] = (uint8_t)(crc >> 8);
        TRY(exchange(r, frame, 9U, 0U, sak, 3U));
        crc = rc522_crc_a(sak, 1U);
        if (sak[1] != (uint8_t)crc || sak[2] != (uint8_t)(crc >> 8) ||
            (!!(sak[0] & 4U) != !!ct)) return LOCKER_STATUS_IO_ERROR;
        for (i = ct ? 1U : 0U; i < 4U; ++i) result.bytes[result.length++] = block[i];
        if (!(sak[0] & 4U)) {
            locker_status_t halt_status;
            if (result.length != 4U && result.length != 7U && result.length != 10U)
                return LOCKER_STATUS_IO_ERROR;
            /* Put the selected card into HALT so the next WUPA can see it.
               A compliant card intentionally does not answer HLTA. */
            frame[0] = 0x50U; frame[1] = 0U;
            crc = rc522_crc_a(frame, 2U);
            frame[2] = (uint8_t)crc; frame[3] = (uint8_t)(crc >> 8);
            halt_status = exchange(r, frame, 4U, 0U, sak, 0U);
            if (halt_status != LOCKER_STATUS_NOT_READY)
                return halt_status == LOCKER_STATUS_OK ? LOCKER_STATUS_IO_ERROR : halt_status;
            *uid = result;
            return LOCKER_STATUS_OK;
        }
    }
    return LOCKER_STATUS_IO_ERROR;
}
