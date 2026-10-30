#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "w25q128.h"

/* Protocol fixture only. Real Flash/power-loss acceptance is separate. */
static uint8_t memory[0x1000000];
static struct {
    uint64_t now;
    uint32_t id, programs, erases, reads, waits, transfers;
    uint8_t sr1, sr2, sr3, busy;
    bool active, freeze_clock, fail_transfer, ignore_wren, ignore_program, ignore_erase, never_ready, timeout_write, hold_wel;
} f;
static const mcu_journal_config_t geometry = {true, 0xef4018, 0x1000000, 4096, 256, 0};
static locker_status_t transfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    uint32_t address, i;
    (void)ctx;
    assert(!f.active && n != 0 && n <= 260);
    f.active = true; ++f.transfers;
    memset(rx, 0, n);
    if (f.fail_transfer) { f.active = false; return LOCKER_STATUS_IO_ERROR; }
    switch (tx[0]) {
    case 0x9f:
        assert(n == 4); rx[1] = (uint8_t)(f.id >> 16); rx[2] = (uint8_t)(f.id >> 8); rx[3] = (uint8_t)f.id; break;
    case 0x05:
        assert(n == 2);
        rx[1] = f.sr1 | ((f.never_ready || f.busy != 0) ? 1 : 0);
        if (f.busy != 0) --f.busy;
        break;
    case 0x35: assert(n == 2); rx[1] = f.sr2; break;
    case 0x15: assert(n == 2); rx[1] = f.sr3; break;
    case 0x06:
        assert(n == 1); if (!f.ignore_wren) f.sr1 |= 2; break;
    case 0x03: case 0x02: case 0x20:
        assert(n >= 4);
        address = ((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3];
        assert(address < sizeof(memory));
        if (tx[0] == 0x03) {
            assert(address + n - 4 <= sizeof(memory));
            memcpy(rx + 4, memory + address, n - 4); ++f.reads;
        } else if (tx[0] == 0x02) {
            assert((f.sr1 & 3) == 2 && n > 4 && n <= 260);
            assert(address % 256 + n - 4 <= 256); /* Never wrap a page. */
            if (!f.ignore_program) for (i = 0; i < (uint32_t)n - 4U; ++i) memory[address + i] &= tx[i + 4];
            if (!f.hold_wel) f.sr1 &= (uint8_t)~2U;
            f.busy = 2; ++f.programs;
            if (f.timeout_write) f.never_ready = true;
        } else {
            assert((f.sr1 & 3) == 2 && n == 4 && address % 4096 == 0);
            if (!f.ignore_erase) memset(memory + address, 0xff, 4096);
            f.sr1 &= (uint8_t)~2U; f.busy = 3; ++f.erases;
        }
        break;
    default: assert(0);
    }
    f.active = false;
    return LOCKER_STATUS_OK;
}
static uint64_t now_ms(void *ctx) { (void)ctx; return f.now; }
static void wait_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    assert(!f.active); /* Busy waits happen after CS and bus lock are released. */
    ++f.waits; if (!f.freeze_clock) f.now += ms;
}
static void setup(w25q128_t *d)
{
    memset(&f, 0, sizeof(f)); memset(d, 0, sizeof(*d));
    f.id = 0xef4018; d->transfer = transfer; d->now_ms = now_ms; d->wait_ms = wait_ms;
}
int main(void)
{
    w25q128_t d;
    mcu_journal_config_t config;
    const struct lfs_config *c;
    uint8_t data[600], readback[600];
    uint32_t calls;
    memset(memory, 0xff, sizeof(memory)); memset(data, 0x5a, sizeof(data));
    setup(&d); config = geometry; config.geometry_verified = false;
    assert(w25q128_init(&d, &config) == LOCKER_STATUS_NOT_READY && f.transfers == 0);
    config = geometry; config.capacity_bytes--;
    assert(w25q128_init(&d, &config) == LOCKER_STATUS_NOT_READY && f.transfers == 0);
    f.id = 0xef4017;
    assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_NOT_READY && !d.ready);
    setup(&d); f.sr2 = 0x80;
    assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_IO_ERROR && !d.ready);
    setup(&d); f.sr3 = 4;
    assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_IO_ERROR && !d.ready);
    setup(&d);
    assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    c = w25q128_lfs_config(&d);
    assert(c && c->context == &d && c->block_count == 4096 && c->block_size == 4096);
    assert(c->read_buffer == d.read_cache && c->prog_buffer == d.prog_cache && c->lookahead_buffer == d.lookahead);
    assert(w25q128_program(&d, 0x00fffe00, data, 512) == LOCKER_STATUS_OK && f.programs == 2);
    assert(w25q128_program(&d, 0xf1, data, sizeof(data)) == LOCKER_STATUS_OK && f.programs == 6);
    assert(w25q128_read(&d, 0xf1, readback, sizeof(readback)) == LOCKER_STATUS_OK && memcmp(data, readback, sizeof(data)) == 0);
    assert(w25q128_erase_sector(&d, 0) == LOCKER_STATUS_OK && f.erases == 1 && f.waits > 0);
    assert(memory[0xf1] == 0xff && memory[4095] == 0xff);
    calls = f.transfers;
    assert(w25q128_read(&d, 0x1000000, readback, 1) == LOCKER_STATUS_INVALID);
    assert(w25q128_program(&d, 0xffffff, data, 2) == LOCKER_STATUS_INVALID);
    assert(w25q128_program(&d, 0xffffff, data, UINT32_MAX) == LOCKER_STATUS_INVALID);
    assert(w25q128_erase_sector(&d, 1) == LOCKER_STATUS_INVALID);
    assert(c->read(c, 4096, 0, readback, 16) == LFS_ERR_INVAL);
    assert(c->prog(c, 0, 1, data, 256) == LFS_ERR_INVAL);
    assert(c->read(c, 0, 4080, readback, 32) == LFS_ERR_INVAL);
    assert(c->erase(c, 4096) == LFS_ERR_INVAL && f.transfers == calls);
    assert(c->prog(c, 1, 0, data, 256) == 0);
    assert(c->read(c, 1, 0, readback, 256) == 0 && memcmp(data, readback, 256) == 0);
    assert(c->sync(c) == 0);
    f.ignore_wren = true; calls = f.programs;
    assert(w25q128_program(&d, 4096, data, 1) == LOCKER_STATUS_IO_ERROR && !d.ready && f.programs == calls);
    assert(w25q128_program(&d, 4096, data, 1) == LOCKER_STATUS_NOT_READY);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    f.ignore_program = true;
    assert(w25q128_program(&d, 8192, data, 1) == LOCKER_STATUS_IO_ERROR && !d.ready);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    f.timeout_write = true;
    assert(w25q128_program(&d, 8192, data, 1) == LOCKER_STATUS_IO_ERROR && !d.ready && f.programs == 1);
    assert(w25q128_program(&d, 8192, data, 1) == LOCKER_STATUS_NOT_READY && f.programs == 1);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    f.hold_wel = true;
    assert(w25q128_program(&d, 8192, data, 1) == LOCKER_STATUS_IO_ERROR && !d.ready);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    f.sr1 = 4;
    assert(w25q128_program(&d, 8192, data, 1) == LOCKER_STATUS_IO_ERROR && !d.ready && f.programs == 0);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    memory[12288] = 0; f.ignore_erase = true;
    assert(w25q128_erase_sector(&d, 12288) == LOCKER_STATUS_IO_ERROR && !d.ready);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    f.never_ready = f.freeze_clock = true;
    assert(w25q128_sync(&d) == LOCKER_STATUS_IO_ERROR && !d.ready && f.waits <= 1001);
    setup(&d); assert(w25q128_init(&d, &geometry) == LOCKER_STATUS_OK);
    f.fail_transfer = true;
    assert(w25q128_read(&d, 0, readback, 1) == LOCKER_STATUS_IO_ERROR && !d.ready);
    assert(w25q128_lfs_config(&d) == NULL);
    puts("flash protocol, geometry, page split, bounds, readback, protection, timeout and failure latch PASS");
    return 0;
}
