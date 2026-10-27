#ifndef LOCKER_STORAGE_U64_H
#define LOCKER_STORAGE_U64_H

/* 64 位计数在契约里一律是「已校验十进制文本」（internal-data.schema.json 的 U64）。
 * 仓储按数值比较，绝不使用 SQLite 数值列或字典序比较（x-rules 12）。 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 解析 ^(0|[1-9][0-9]{0,19})$ 且不超过 UINT64_MAX。length 不含结尾 NUL。 */
bool locker_storage_u64_parse(const char *text, size_t length, uint64_t *out);

/* 写规范十进制文本（无前导零），out_size 至少 21 字节。 */
bool locker_storage_u64_format(uint64_t value, char *out, size_t out_size);

/* 数值比较两个已校验十进制文本，返回 <0/0/>0；不做溢出、不做浮点。 */
int locker_storage_u64_compare_text(const char *left, const char *right);

#endif
