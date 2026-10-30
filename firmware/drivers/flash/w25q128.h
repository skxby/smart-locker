#ifndef LOCKER_W25Q128_H
#define LOCKER_W25Q128_H
#include "locker_contracts.h"
#include "lfs.h"

/* One PeripheralStorage owner. Callbacks and this object outlive the mount.
   transfer must assert/deassert CS for the entire transaction and release the
   SPI mutex before returning. wait_ms never runs with CS or the mutex held. */
typedef struct {
    void *context;
    locker_status_t (*transfer)(void *, const uint8_t *, uint8_t *, uint16_t);
    uint64_t (*now_ms)(void *);
    void (*wait_ms)(void *, uint32_t);
    struct lfs_config lfs;
    uint8_t read_cache[256], prog_cache[256], lookahead[32];
    uint8_t tx[260], rx[260];
    uint32_t jedec_id;
    locker_status_t last_error;
    bool ready;
} w25q128_t;

locker_status_t w25q128_init(w25q128_t *, const mcu_journal_config_t *);
const struct lfs_config *w25q128_lfs_config(w25q128_t *);
locker_status_t w25q128_read(w25q128_t *, uint32_t address, void *, uint32_t size);
locker_status_t w25q128_program(w25q128_t *, uint32_t address, const void *, uint32_t size);
locker_status_t w25q128_erase_sector(w25q128_t *, uint32_t address);
locker_status_t w25q128_sync(w25q128_t *);
#endif
