#ifndef LOCKER_DOMAIN_TIME_H
#define LOCKER_DOMAIN_TIME_H

/*
 * S1-04 业务核心私有时间工具：RFC 3339（UTC，秒级或毫秒级小数）与毫秒换算。
 *
 * 存储层的 format date-time 校验器（host/storage/src/schema.c）接受
 * YYYY-MM-DDTHH:MM:SSZ 与带小数秒的形式，拒绝其它偏移写法以外的简化。这里只
 * 生成最简的 UTC 'Z' 形式，解析则接受 'Z' 与 ±HH:MM（读取外部写入的会话过期时间）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 需要至少 21 字节：'YYYY-MM-DDTHH:MM:SSZ' 共 20 字符 + NUL。 */
bool locker_domain_time_format_utc(uint64_t wall_utc_ms, char *out, size_t out_size);
/* 解析 RFC 3339 到 UTC 毫秒；不支持闰秒与 60 秒以上的秒值。 */
bool locker_domain_time_parse_utc(const char *text, size_t length, uint64_t *out_wall_ms);

#endif /* LOCKER_DOMAIN_TIME_H */
