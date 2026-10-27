#ifndef LOCKER_STORAGE_SCHEMA_H
#define LOCKER_STORAGE_SCHEMA_H

/*
 * 契约 schema 校验器（模块内部头文件，不是契约公共 API）。
 *
 * 校验对象是 contracts/internal-data.schema.json 的记录闭包子集：该子集由
 * src/schema_records.json 冻结，构建期嵌入成 C 字符串（见 embedded.h）。
 * 运行时用同一份真源做校验，不手抄第二套字段表，也不引入第二个 schema 真源。
 *
 * 支持的 JSON Schema 关键字严格限于该子集实际用到的：
 *   $ref / type / properties / required / additionalProperties(false) /
 *   const / enum / pattern / format(uuid,date-time) / minLength / maxLength /
 *   minimum / maximum / minItems / maxItems / items / anyOf / allOf / if-then
 * 遇到不认识的关键字一律按「校验失败」处理（fail closed），不放行。
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct locker_storage_schema locker_storage_schema_t;

#define LOCKER_STORAGE_COLLECTION_COUNT 18u
#define LOCKER_STORAGE_COLLECTION_NAME_MAX 32u

/* 解析嵌入的 schema 子集；失败返回 NULL（表示构建产物损坏，属致命错误）。 */
locker_storage_schema_t *locker_storage_schema_create(void);
void locker_storage_schema_destroy(locker_storage_schema_t *schema);

/* 受控集合名 <-> 索引（按名字字节序，与 x-collection-schemas 的键序一致）。 */
const char *locker_storage_schema_collection_name(size_t index);
bool locker_storage_schema_collection_lookup(const char *name, size_t length, size_t *out_index);

/* 解析 JSON 文本并按 $defs 名字校验；解析失败或校验失败都返回 false。 */
bool locker_storage_schema_validate_json(locker_storage_schema_t *schema, const char *def_name,
                                         const char *json, size_t length);

/* 按集合校验文档 value（x-rules 02：必须满足该集合对应记录，而不是 anyOf 任意分支）。 */
bool locker_storage_schema_validate_document_json(locker_storage_schema_t *schema,
                                                  size_t collection_index, const char *json,
                                                  size_t length);

#endif
