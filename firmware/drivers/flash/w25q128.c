/* W25Q128 1-1-1 SPI NOR block device. No format, chip erase, protection changes,
   HAL handles, heap allocation or lock/Control access. */
#include <string.h>
#include "w25q128.h"

#define CAPACITY 0x1000000UL
#define SECTOR 4096U
#define PAGE 256U
#define TRY(call) do { locker_status_t s_ = (call); if (s_ != LOCKER_STATUS_OK) return s_; } while (0)

static locker_status_t transact(w25q128_t *d, uint16_t n)
{
    locker_status_t s = d->transfer(d->context, d->tx, d->rx, n);
    if (s != LOCKER_STATUS_OK) { d->last_error = s; d->ready = false; }
    return s;
}
static locker_status_t status(w25q128_t *d, uint8_t command, uint8_t *value)
{
    d->tx[0] = command; d->tx[1] = 0U;
    TRY(transact(d, 2U));
    *value = d->rx[1];
    return LOCKER_STATUS_OK;
}
static locker_status_t failure(w25q128_t *d)
{
    d->ready = false; d->last_error = LOCKER_STATUS_IO_ERROR;
    return LOCKER_STATUS_IO_ERROR;
}
static locker_status_t wait_ready(w25q128_t *d, uint32_t timeout)
{
    uint64_t start = d->now_ms(d->context), now;
    uint32_t polls;
    uint8_t value;
    /* Poll cap also bounds a broken/stopped clock callback. */
    for (polls = 0U; polls <= timeout; ++polls) {
        TRY(status(d, 0x05U, &value));
        if (!(value & 1U)) return LOCKER_STATUS_OK;
        now = d->now_ms(d->context);
        if (now < start || now - start >= timeout) break;
        d->wait_ms(d->context, 1U);
    }
    return failure(d);
}
static bool range(uint32_t address, uint32_t size)
{
    return size != 0U && address < CAPACITY && size <= CAPACITY - address;
}
static void command_address(w25q128_t *d, uint8_t command, uint32_t address)
{
    d->tx[0] = command; d->tx[1] = (uint8_t)(address >> 16);
    d->tx[2] = (uint8_t)(address >> 8); d->tx[3] = (uint8_t)address;
}
static locker_status_t write_enable(w25q128_t *d)
{
    uint8_t sr1, sr2, sr3;
    TRY(wait_ready(d, 1000U));
    TRY(status(d, 0x05U, &sr1));
    TRY(status(d, 0x35U, &sr2));
    TRY(status(d, 0x15U, &sr3));
    /* Refuse protected parts rather than silently clearing user protection. */
    if ((sr1 & 0x7cU) || (sr2 & 0xc0U) || (sr3 & 4U)) return failure(d);
    d->tx[0] = 0x06U; TRY(transact(d, 1U));
    TRY(status(d, 0x05U, &sr1));
    if ((sr1 & 3U) != 2U) return failure(d);
    return LOCKER_STATUS_OK;
}
static locker_status_t write_complete(w25q128_t *d, uint32_t timeout)
{
    uint8_t sr1;
    TRY(wait_ready(d, timeout));
    TRY(status(d, 0x05U, &sr1));
    if (sr1 & 3U) return failure(d);
    return LOCKER_STATUS_OK;
}
locker_status_t w25q128_read(w25q128_t *d, uint32_t address, void *buffer, uint32_t size)
{
    uint8_t *out = buffer;
    if (d == NULL || buffer == NULL || !range(address, size)) return LOCKER_STATUS_INVALID;
    if (!d->ready) return LOCKER_STATUS_NOT_READY;
    TRY(wait_ready(d, 1000U));
    while (size != 0U) {
        uint16_t n = size > PAGE ? PAGE : (uint16_t)size;
        command_address(d, 0x03U, address);
        memset(d->tx + 4U, 0xff, n);
        TRY(transact(d, (uint16_t)(n + 4U)));
        memcpy(out, d->rx + 4U, n);
        out += n; address += n; size -= n;
    }
    return LOCKER_STATUS_OK;
}
locker_status_t w25q128_program(w25q128_t *d, uint32_t address, const void *buffer, uint32_t size)
{
    const uint8_t *in = buffer;
    uint8_t verify[PAGE];
    if (d == NULL || buffer == NULL || !range(address, size)) return LOCKER_STATUS_INVALID;
    if (!d->ready) return LOCKER_STATUS_NOT_READY;
    while (size != 0U) {
        uint16_t n = (uint16_t)(PAGE - (address % PAGE));
        if (size < n) n = (uint16_t)size;
        TRY(write_enable(d));
        command_address(d, 0x02U, address); memcpy(d->tx + 4U, in, n);
        TRY(transact(d, (uint16_t)(n + 4U)));
        TRY(write_complete(d, 10U));
        TRY(w25q128_read(d, address, verify, n));
        if (memcmp(verify, in, n) != 0) return failure(d);
        in += n; address += n; size -= n;
    }
    return LOCKER_STATUS_OK;
}
locker_status_t w25q128_erase_sector(w25q128_t *d, uint32_t address)
{
    uint32_t off;
    uint16_t i;
    uint8_t verify[PAGE];
    if (d == NULL || address % SECTOR != 0U || !range(address, SECTOR)) return LOCKER_STATUS_INVALID;
    if (!d->ready) return LOCKER_STATUS_NOT_READY;
    TRY(write_enable(d)); command_address(d, 0x20U, address);
    TRY(transact(d, 4U)); TRY(write_complete(d, 1000U));
    for (off = 0U; off < SECTOR; off += PAGE) {
        TRY(w25q128_read(d, address + off, verify, PAGE));
        for (i = 0U; i < PAGE; ++i) if (verify[i] != 0xffU) return failure(d);
    }
    return LOCKER_STATUS_OK;
}
locker_status_t w25q128_sync(w25q128_t *d)
{
    if (d == NULL) return LOCKER_STATUS_INVALID;
    if (!d->ready) return LOCKER_STATUS_NOT_READY;
    return wait_ready(d, 1000U);
}
static bool block_range(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, lfs_size_t size)
{
    return block < c->block_count && off < c->block_size && size != 0U && size <= c->block_size - off;
}
static int bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    if (!block_range(c, block, off, size) || off % c->read_size || size % c->read_size) return LFS_ERR_INVAL;
    return w25q128_read(c->context, block * SECTOR + off, buffer, size) == LOCKER_STATUS_OK ? 0 : LFS_ERR_IO;
}
static int bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    if (!block_range(c, block, off, size) || off % c->prog_size || size % c->prog_size) return LFS_ERR_INVAL;
    return w25q128_program(c->context, block * SECTOR + off, buffer, size) == LOCKER_STATUS_OK ? 0 : LFS_ERR_IO;
}
static int bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    if (block >= c->block_count) return LFS_ERR_INVAL;
    return w25q128_erase_sector(c->context, block * SECTOR) == LOCKER_STATUS_OK ? 0 : LFS_ERR_IO;
}
static int bd_sync(const struct lfs_config *c)
{
    return w25q128_sync(c->context) == LOCKER_STATUS_OK ? 0 : LFS_ERR_IO;
}
locker_status_t w25q128_init(w25q128_t *d, const mcu_journal_config_t *config)
{
    uint8_t sr1, sr2, sr3;
    if (d == NULL || config == NULL) return LOCKER_STATUS_INVALID;
    d->ready = false; d->jedec_id = 0U; d->last_error = LOCKER_STATUS_NOT_READY;
    memset(&d->lfs, 0, sizeof(d->lfs));
    if (d->transfer == NULL || d->now_ms == NULL || d->wait_ms == NULL) return LOCKER_STATUS_INVALID;
    if (!config->geometry_verified || config->jedec_id != 0xef4018UL ||
        config->capacity_bytes != CAPACITY || config->sector_bytes != SECTOR ||
        config->page_bytes != PAGE || config->chip_select != 0U) return LOCKER_STATUS_NOT_READY;
    TRY(wait_ready(d, 1000U));
    d->tx[0] = 0x9fU; d->tx[1] = d->tx[2] = d->tx[3] = 0U;
    TRY(transact(d, 4U));
    d->jedec_id = ((uint32_t)d->rx[1] << 16) | ((uint32_t)d->rx[2] << 8) | d->rx[3];
    if (d->jedec_id != config->jedec_id) return LOCKER_STATUS_NOT_READY;
    TRY(status(d, 0x05U, &sr1)); TRY(status(d, 0x35U, &sr2));
    TRY(status(d, 0x15U, &sr3));
    if ((sr1 & 0x7cU) || (sr2 & 0xc0U) || (sr3 & 4U)) return failure(d);
    d->lfs.context = d; d->lfs.read = bd_read; d->lfs.prog = bd_prog;
    d->lfs.erase = bd_erase; d->lfs.sync = bd_sync;
    d->lfs.read_size = 16U; d->lfs.prog_size = PAGE;
    d->lfs.block_size = SECTOR; d->lfs.block_count = CAPACITY / SECTOR;
    d->lfs.block_cycles = 500; d->lfs.cache_size = PAGE; d->lfs.lookahead_size = sizeof(d->lookahead);
    d->lfs.read_buffer = d->read_cache; d->lfs.prog_buffer = d->prog_cache;
    d->lfs.lookahead_buffer = d->lookahead;
    d->ready = true; d->last_error = LOCKER_STATUS_OK;
    return LOCKER_STATUS_OK;
}
const struct lfs_config *w25q128_lfs_config(w25q128_t *d)
{
    return d != NULL && d->ready ? &d->lfs : NULL;
}
