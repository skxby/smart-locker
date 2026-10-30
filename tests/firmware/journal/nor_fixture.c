#include "nor_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static nor_fixture_t *checked(const struct lfs_config *c, lfs_block_t block,
                              lfs_off_t off, lfs_size_t size)
{
    nor_fixture_t *nor = c->context;
    assert(block < c->block_count);
    assert(off <= c->block_size && size <= c->block_size - off);
    return nor;
}

static int read_block(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size)
{
    nor_fixture_t *nor = checked(c, block, off, size);
    ++nor->reads;
    if (nor->failed) return LFS_ERR_IO;
    memcpy(buffer, nor->bytes + block * c->block_size + off, size);
    return 0;
}

static int cut(nor_fixture_t *nor)
{
    ++nor->operations;
    return nor->fail_at && nor->operations == nor->fail_at;
}

static int program_block(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size)
{
    nor_fixture_t *nor = checked(c, block, off, size);
    const uint8_t *input = buffer;
    uint8_t *output = nor->bytes + block * c->block_size + off;
    lfs_size_t i, count;
    int interrupted;
    ++nor->programs;
    if (nor->failed) return LFS_ERR_IO;
    assert(off % c->prog_size == 0 && size % c->prog_size == 0);
    interrupted = cut(nor);
    count = interrupted ? size / 2 : size;
    for (i = 0; i < count; ++i) {
        assert((uint8_t)(output[i] & input[i]) == input[i]);
        output[i] &= input[i];
    }
    if (interrupted) { nor->failed = 1; return LFS_ERR_IO; }
    return 0;
}

static int erase_block(const struct lfs_config *c, lfs_block_t block)
{
    nor_fixture_t *nor = checked(c, block, 0, c->block_size);
    int interrupted;
    ++nor->erases;
    if (nor->failed) return LFS_ERR_IO;
    interrupted = cut(nor);
    memset(nor->bytes + block * c->block_size, 0xff,
           interrupted ? c->block_size / 2 : c->block_size);
    if (interrupted) { nor->failed = 1; return LFS_ERR_IO; }
    return 0;
}

static int sync_block(const struct lfs_config *c)
{
    nor_fixture_t *nor = c->context;
    ++nor->syncs;
    if (nor->failed) return LFS_ERR_IO;
    if (cut(nor)) { nor->failed = 1; return LFS_ERR_IO; }
    return 0;
}

void nor_fixture_restart(nor_fixture_t *nor)
{
    nor->operations = nor->fail_at = 0;
    nor->failed = 0;
    memset(nor->read_cache, 0, sizeof(nor->read_cache));
    memset(nor->prog_cache, 0, sizeof(nor->prog_cache));
    memset(nor->lookahead, 0, sizeof(nor->lookahead));
}

void nor_fixture_init(nor_fixture_t *nor, unsigned block_count)
{
    memset(nor, 0, sizeof(*nor));
    nor->capacity = (size_t)block_count * 4096;
    nor->bytes = malloc(nor->capacity);
    assert(nor->bytes);
    memset(nor->bytes, 0xff, nor->capacity);
    nor->config.context = nor;
    nor->config.read = read_block;
    nor->config.prog = program_block;
    nor->config.erase = erase_block;
    nor->config.sync = sync_block;
    nor->config.read_size = 16;
    nor->config.prog_size = 256;
    nor->config.block_size = 4096;
    nor->config.block_count = block_count;
    nor->config.block_cycles = 100;
    nor->config.cache_size = 256;
    nor->config.lookahead_size = sizeof(nor->lookahead);
    nor->config.read_buffer = nor->read_cache;
    nor->config.prog_buffer = nor->prog_cache;
    nor->config.lookahead_buffer = nor->lookahead;
}

void nor_fixture_cut_at(nor_fixture_t *nor, unsigned operation)
{
    nor->operations = 0;
    nor->fail_at = operation;
    nor->failed = 0;
}

void nor_fixture_destroy(nor_fixture_t *nor)
{
    free(nor->bytes);
    memset(nor, 0, sizeof(*nor));
}
