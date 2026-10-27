#ifndef LOCKER_DOMAIN_SHA256_H
#define LOCKER_DOMAIN_SHA256_H

/*
 * S1-04 业务核心私有 SHA-256（FIPS 180-4）。
 *
 * 只用于幂等记录的请求指纹（internal-data.schema.json 的 IdempotencyRecord.request_hash
 * 要求 64 位十六进制）。它不是密码哈希：口令哈希与会话令牌摘要属于 S1-05，
 * 需要 Argon2id 与真正的 salt，本模块不假装能做（见 host/domain/README.md §8）。
 * 算法本身是公开标准，自实现避免把未 vendor 的第三方库引入依赖锁（dependencies.lock.json
 * 的 verified 门禁）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct locker_domain_sha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t block_used;
    bool finalized;
} locker_domain_sha256_t;

void locker_domain_sha256_init(locker_domain_sha256_t *ctx);
void locker_domain_sha256_update(locker_domain_sha256_t *ctx, const void *data, size_t length);
/* 写入 32 字节摘要；同一个 ctx 只能 finalize 一次。 */
void locker_domain_sha256_final(locker_domain_sha256_t *ctx, uint8_t out_digest[32]);

/* 一次性摘要；digest 至少 32 字节。 */
void locker_domain_sha256(const void *data, size_t length, uint8_t out_digest[32]);
/* 摘要转 64 位小写十六进制加 NUL；out_size >= 65。 */
bool locker_domain_sha256_hex(const void *data, size_t length, char *out_hex, size_t out_size);

#endif /* LOCKER_DOMAIN_SHA256_H */
