#define _POSIX_C_SOURCE 200809L

#include "domain_sha256.h"

#include <string.h>

static const uint32_t k_round[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t rotr(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_transform(locker_domain_sha256_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    for (unsigned i = 0u; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16) |
               ((uint32_t)block[i * 4u + 2u] << 8) | (uint32_t)block[i * 4u + 3u];
    }
    for (unsigned i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];
    for (unsigned i = 0u; i < 64u; ++i) {
        uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k_round[i] + w[i];
        uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void locker_domain_sha256_init(locker_domain_sha256_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->total_bytes = 0u;
    ctx->block_used = 0u;
    ctx->finalized = false;
    memset(ctx->block, 0, sizeof(ctx->block));
}

void locker_domain_sha256_update(locker_domain_sha256_t *ctx, const void *data, size_t length)
{
    if (ctx == NULL || ctx->finalized || (data == NULL && length > 0u)) {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)data;
    ctx->total_bytes += (uint64_t)length;
    for (size_t i = 0u; i < length; ++i) {
        ctx->block[ctx->block_used++] = bytes[i];
        if (ctx->block_used == 64u) {
            sha256_transform(ctx, ctx->block);
            ctx->block_used = 0u;
        }
    }
}

void locker_domain_sha256_final(locker_domain_sha256_t *ctx, uint8_t out_digest[32])
{
    if (ctx == NULL || out_digest == NULL || ctx->finalized) {
        return;
    }
    uint64_t bit_length = ctx->total_bytes * 8u;
    uint8_t pad = 0x80u;
    locker_domain_sha256_update(ctx, &pad, 1u);
    ctx->finalized = true; /* 之后的 update 被忽略 */
    uint8_t zero = 0u;
    while (ctx->block_used != 56u) {
        ctx->block[ctx->block_used++] = zero;
        if (ctx->block_used == 64u) {
            sha256_transform(ctx, ctx->block);
            ctx->block_used = 0u;
        }
    }
    for (unsigned i = 0u; i < 8u; ++i) {
        ctx->block[56u + i] = (uint8_t)((bit_length >> (56u - 8u * i)) & 0xffu);
    }
    sha256_transform(ctx, ctx->block);
    for (unsigned i = 0u; i < 8u; ++i) {
        out_digest[i * 4u] = (uint8_t)((ctx->state[i] >> 24) & 0xffu);
        out_digest[i * 4u + 1u] = (uint8_t)((ctx->state[i] >> 16) & 0xffu);
        out_digest[i * 4u + 2u] = (uint8_t)((ctx->state[i] >> 8) & 0xffu);
        out_digest[i * 4u + 3u] = (uint8_t)(ctx->state[i] & 0xffu);
    }
}

void locker_domain_sha256(const void *data, size_t length, uint8_t out_digest[32])
{
    locker_domain_sha256_t ctx;
    locker_domain_sha256_init(&ctx);
    locker_domain_sha256_update(&ctx, data, length);
    locker_domain_sha256_final(&ctx, out_digest);
}

bool locker_domain_sha256_hex(const void *data, size_t length, char *out_hex, size_t out_size)
{
    static const char k_hex[] = "0123456789abcdef";
    if (out_hex == NULL || out_size < 65u) {
        return false;
    }
    uint8_t digest[32];
    locker_domain_sha256(data, length, digest);
    for (size_t i = 0u; i < sizeof(digest); ++i) {
        out_hex[i * 2u] = k_hex[(digest[i] >> 4) & 0x0fu];
        out_hex[i * 2u + 1u] = k_hex[digest[i] & 0x0fu];
    }
    out_hex[64] = '\0';
    return true;
}
