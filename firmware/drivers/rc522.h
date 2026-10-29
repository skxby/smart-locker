#ifndef LOCKER_RC522_H
#define LOCKER_RC522_H
#include "locker_contracts.h"
typedef struct {
    void *context;
    locker_status_t (*transfer)(void *, const uint8_t *, uint8_t *, uint16_t);
    uint64_t (*now_ms)(void *);
    void (*wait_ms)(void *, uint32_t);
    bool ready;
    uint8_t version;
    uint8_t last_irq, last_error, last_count, last_bits, last_command;
} rc522_t;
typedef struct { uint8_t length, bytes[10]; } rc522_uid_t;
locker_status_t rc522_init(rc522_t *);
locker_status_t rc522_read_uid(rc522_t *, rc522_uid_t *);
uint16_t rc522_crc_a(const uint8_t *, uint8_t);
#endif
