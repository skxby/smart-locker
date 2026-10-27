#ifndef LOCKER_STORAGE_EVENT_KEY_H
#define LOCKER_STORAGE_EVENT_KEY_H

/* 设备事实键的文档 key 编码（internal-data.schema.json x-rules 07）：
 *   <device_id 24 hex>/<journal_epoch uuid 36>/<boot_id 20 位补零>/<event_seq 20 位补零>
 * 共 24+1+36+1+20+1+20 = 103 字节。20 位补零让 BINARY 排序等于数值排序，
 * 于是 "10" 不会排在 "2" 之前。 */

#include <stdbool.h>
#include <stddef.h>

#include "locker_contracts.h"

#define LOCKER_STORAGE_EVENT_KEY_TEXT_MAX 104
#define LOCKER_STORAGE_IMPORT_CURSOR_KEY_TEXT_MAX 62

/* 事件键 -> 文档 key；越界、非法字段一律失败，不截断。 */
bool locker_storage_event_key_build(const locker_event_key_t *key, char *out, size_t out_size);

/* 文档 key -> 事件键；严格 103 字节、字段长度与字符集全部校验。 */
bool locker_storage_event_key_parse(const char *text, locker_event_key_t *out);

/* 导入游标 key：<device_id>/<journal_epoch>，同一设备同一日志 epoch 一条游标。 */
bool locker_storage_import_cursor_key_build(const locker_event_key_t *key, char *out, size_t out_size);

/* 同一 device/epoch 内是否 (0,0) 哨兵（尚无游标）。 */
bool locker_storage_event_key_is_zero(const locker_event_key_t *key);

/* 数值序比较：device_id 字节序、epoch 字节序、boot_id、event_seq。 */
int locker_storage_event_key_compare(const locker_event_key_t *left, const locker_event_key_t *right);

/* 同 boot 的下一个事件序号；event_seq 为 UINT64_MAX 或 key 非法时返回 false。 */
bool locker_storage_event_key_next_seq(const locker_event_key_t *current, locker_event_key_t *next);

#endif
