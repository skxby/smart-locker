#ifndef LOCKER_STORAGE_REGEX_H
#define LOCKER_STORAGE_REGEX_H

/*
 * 受限正则引擎：只实现 internal-data.schema.json 冻结契约里实际用到的
 * JSON Schema "pattern" 语法子集（^ $ . [ ] 区间 分组 | * + ? {n} {n,m} {n,}
 * 以及 \d \w \s 转义）。不支持反向引用、前瞻、Unicode 属性等；遇到不支持的
 * 语法编译失败，调用方必须按「校验失败」处理（fail closed），不能放行。
 *
 * 语义与 JSON Schema 一致：pattern 默认是「搜索」而不是「整串匹配」，
 * 因此没有 ^...$ 锚点的模式会在任意起点尝试匹配。
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct locker_storage_regex locker_storage_regex_t;

/* 编译失败（语法不支持或状态数超限）返回 NULL。 */
locker_storage_regex_t *locker_storage_regex_compile(const char *pattern);
void locker_storage_regex_free(locker_storage_regex_t *regex);

/* 在 text 中搜索匹配；text 不必以 NUL 结尾，长度显式给出。 */
bool locker_storage_regex_match(const locker_storage_regex_t *regex, const char *text, size_t length);

#endif
