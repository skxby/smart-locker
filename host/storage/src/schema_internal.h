#ifndef LOCKER_STORAGE_SCHEMA_INTERNAL_H
#define LOCKER_STORAGE_SCHEMA_INTERNAL_H

/* schema 校验器的 cJSON 层入口（仅仓储内部使用，不对外暴露 cJSON 类型）。 */

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "locker/storage/schema.h"

/* 拒绝无法在 C 字符串里无损表示的 JSON 文本（嵌入 NUL）。cJSON 把 \u0000 解码成
   U+0000 存进 valuestring，后续 strlen 会静默截断，必须在使用 cJSON 之前拦下。
   返回 true 表示文本不可表示（调用方按 INVALID/解析失败处理）。 */
bool locker_storage_json_text_has_embedded_nul(const char *json, size_t length);

bool locker_storage_schema_validate_def_value(locker_storage_schema_t *schema, const char *def_name,
                                              const cJSON *value);

bool locker_storage_schema_validate_document_value(locker_storage_schema_t *schema,
                                                   size_t collection_index, const cJSON *value);

#endif
