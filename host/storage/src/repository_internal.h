#ifndef LOCKER_STORAGE_REPOSITORY_INTERNAL_H
#define LOCKER_STORAGE_REPOSITORY_INTERNAL_H

/*
 * Repository 内部共享声明（模块内部头文件，不是契约公共 API）。
 *
 * 公共入口只有 contracts/include/locker_contracts.h 里的六个 locker_repo_*
 * 端口；这里放它们之间共用的 SQLite 句柄、事务、文档读写与 JSON 辅助，
 * 不对外暴露 cJSON 或 sqlite3 类型。
 *
 * 线程模型：locker_repo_t 只允许 Core 业务线程使用（docs/architecture/
 * module-ownership.md:24）。open/close 是所有者独占的生命周期调用，
 * close 不得与其他调用并发；仓储内部不持有后台线程。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "locker/storage/schema.h"
#include "locker_contracts.h"
#include "schema_internal.h"

struct sqlite3;

#define LOCKER_STORAGE_REPO_SCHEMA_VERSION 1u
#define LOCKER_STORAGE_KEY_MAX 192u
#define LOCKER_STORAGE_PAGE_BYTES_MAX 65535u
#define LOCKER_STORAGE_PAGE_ROWS_MAX 100u
#define LOCKER_STORAGE_EVENT_BATCH_MAX 4u

/* 契约声明 typedef struct locker_repo locker_repo_t; 这里补全定义。 */
struct locker_repo {
    struct sqlite3 *db;
    locker_storage_schema_t *schema;
    bool in_transaction;
};

/* SQLite 错误码 -> 契约状态；未知码按 IO_ERROR 处理（fail closed）。 */
locker_status_t locker_storage_status_from_sqlite(int code);

/* 在 capacity 内找 NUL；找不到（数组未以 NUL 结尾）返回 false。 */
bool locker_storage_text_length(const char *text, size_t capacity, size_t *out_length);

locker_status_t locker_storage_begin_write(locker_repo_t *repo);
locker_status_t locker_storage_begin_read(locker_repo_t *repo);
locker_status_t locker_storage_commit(locker_repo_t *repo);
void locker_storage_rollback(locker_repo_t *repo);

locker_status_t locker_storage_read_revision(locker_repo_t *repo, uint64_t *out_revision);
locker_status_t locker_storage_write_revision(locker_repo_t *repo, uint64_t revision);
locker_status_t locker_storage_document_exists(locker_repo_t *repo, const char *collection,
                                              const char *key, bool *out_exists);
locker_status_t locker_storage_put_document(locker_repo_t *repo, const char *collection, const char *key,
                                            const cJSON *value);
locker_status_t locker_storage_delete_document(locker_repo_t *repo, const char *collection, const char *key);

/* 解析 JSON 文本；require_complete=true 时拒绝值之后的非空白尾巴。 */
cJSON *locker_storage_parse_json(const char *json, size_t length, bool require_complete);

/* 解析 + 按 RepositoryChange 校验；失败时 *out_status 给出原因。 */
cJSON *locker_storage_parse_change(locker_repo_t *repo, const char *json, size_t length,
                                   locker_status_t *out_status);

/* 在已打开的事务里应用一个已校验的 RepositoryChange（检查 revision/checks/mutations）。 */
locker_status_t locker_storage_apply_change(locker_repo_t *repo, const cJSON *change);

/* 把 locker_device_event_t 转成契约 DeviceEventRecord 的 JSON 值（未校验）。 */
cJSON *locker_storage_build_event_record(const locker_device_event_t *event);

bool locker_storage_json_u64(const cJSON *node, uint64_t *out_value);

#endif
