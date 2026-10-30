#ifndef JOURNAL_NOR_FIXTURE_H
#define JOURNAL_NOR_FIXTURE_H
#include "lfs.h"
#include <stddef.h>
#include <stdint.h>

/* Software NOR model only: this fixture does not validate a real flash chip,
   supply collapse, SPI timing, or the board's power-loss behavior. */
typedef struct {
    struct lfs_config config;
    uint8_t *bytes;
    size_t capacity;
    uint8_t read_cache[256], prog_cache[256], lookahead[32];
    unsigned operations, fail_at, reads, programs, erases, syncs;
    int failed;
} nor_fixture_t;

void nor_fixture_init(nor_fixture_t *nor, unsigned block_count);
void nor_fixture_destroy(nor_fixture_t *nor);
void nor_fixture_restart(nor_fixture_t *nor);
/* Operation indices start at 1. The selected write/erase is torn halfway,
   then all IO fails until restart, including reads and attempted retries. */
void nor_fixture_cut_at(nor_fixture_t *nor, unsigned operation);
#endif
