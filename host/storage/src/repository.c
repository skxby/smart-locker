#include "repository_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "locker/common/uuid.h"
#include "locker/storage/embedded.h"
#include "locker/storage/event_key.h"
#include "locker/storage/u64.h"
#include "sqlite3.h"

/* 仓储实现（唯一 SQLite 写入者）。
 *
 * 设计边界（见 docs/architecture/internal-interfaces.md:98-102、
 * docs/architecture/module-ownership.md:24、contracts/internal-data.schema.json 的 x-rules）：
 *   - 只有 Core 业务线程调用；不持有线程、不在回调里发送动作；
 *   - 所有写操作在 BEGIN IMMEDIATE 里一次提交，失败整体回滚；
 *   - 文档 value 用同一份契约 schema 子集校验（x-rules 02），不手抄第二套字段表；
 *   - u64 只以已校验十进制文本存储，比较走数值语义（x-rules 12）。
 */

#define LOCKER_STORAGE_BUSY_TIMEOUT_MS 5000
#define LOCKER_STORAGE_U64_TEXT_MAX 21u
#define LOCKER_STORAGE_UUID_TEXT_MAX 37u
#define LOCKER_STORAGE_DEVICE_ID_MAX 25u

#define LOCKER_STORAGE_COLLECTION_DEVICE_EVENTS "device_events"
#define LOCKER_STORAGE_COLLECTION_IMPORT_CURSORS "import_cursors"

/* ---- 内部函数前向声明（C 需要先声明后使用） ---- */
static locker_status_t exec_sql(sqlite3 *db, const char *sql);
static locker_status_t prepare_stmt(sqlite3 *db, const char *sql, sqlite3_stmt **out_stmt);
static locker_status_t step_write(sqlite3_stmt *stmt);
static locker_status_t db_read_meta(sqlite3 *db, const char *name, char *out_text, size_t out_size);
static locker_status_t db_table_exists(sqlite3 *db, const char *name, bool *out_exists);
static locker_status_t db_object_exists(sqlite3 *db, const char *name, bool *out_exists);
static locker_status_t db_user_object_count(sqlite3 *db, uint64_t *out_count);
static locker_status_t db_read_revision(sqlite3 *db, uint64_t *out_revision);
static locker_status_t db_write_revision(sqlite3 *db, uint64_t revision);
static locker_status_t db_document_exists(sqlite3 *db, const char *collection, const char *key,
                                          bool *out_exists);
static locker_status_t db_put_document(sqlite3 *db, const char *collection, const char *key,
                                       const cJSON *value);
static locker_status_t db_delete_document(sqlite3 *db, const char *collection, const char *key);
static const char *json_string_field(const cJSON *object, const char *name);
static cJSON *build_event_key_json(const locker_event_key_t *key);
static bool event_key_from_json(const cJSON *node, locker_event_key_t *out);
static cJSON *build_cursor_record(const locker_event_key_t *through, bool continuity_ok);
static cJSON *build_document_json(const char *collection, const char *key, const cJSON *value);
static locker_status_t collect_page(sqlite3 *db, const char *collection, const char *after_key,
                                    uint16_t limit, size_t docs_budget, cJSON *documents,
                                    bool *out_has_next, char *out_next, size_t out_next_size);
static locker_status_t write_json_result(cJSON *root, char *out, size_t out_size, size_t *out_length);

/* ---------- 状态映射 ---------- */

locker_status_t locker_storage_status_from_sqlite(int code)
{
    switch (code & 0xff) {
    case SQLITE_OK:
    case SQLITE_DONE:
    case SQLITE_ROW:
        return LOCKER_STATUS_OK;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return LOCKER_STATUS_BUSY;
    case SQLITE_NOMEM:
    case SQLITE_FULL:
        return LOCKER_STATUS_NO_SPACE;
    case SQLITE_CONSTRAINT:
        return LOCKER_STATUS_CONFLICT;
    case SQLITE_MISUSE:
        return LOCKER_STATUS_INVALID;
    default:
        return LOCKER_STATUS_IO_ERROR;
    }
}

bool locker_storage_text_length(const char *text, size_t capacity, size_t *out_length)
{
    if (text == NULL || out_length == NULL || capacity == 0u) {
        return false;
    }
    for (size_t i = 0; i < capacity; ++i) {
        if (text[i] == '\0') {
            *out_length = i;
            return true;
        }
    }
    return false;
}

/* ---------- SQLite 基础封装 ---------- */

static locker_status_t exec_sql(sqlite3 *db, const char *sql)
{
    if (db == NULL || sql == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return locker_storage_status_from_sqlite(sqlite3_exec(db, sql, NULL, NULL, NULL));
}

static locker_status_t prepare_stmt(sqlite3 *db, const char *sql, sqlite3_stmt **out_stmt)
{
    if (db == NULL || sql == NULL || out_stmt == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_stmt = NULL;
    return locker_storage_status_from_sqlite(sqlite3_prepare_v2(db, sql, -1, out_stmt, NULL));
}

static locker_status_t step_write(sqlite3_stmt *stmt)
{
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return LOCKER_STATUS_OK;
    }
    return locker_storage_status_from_sqlite(rc);
}

static locker_status_t bind_text(sqlite3_stmt *stmt, int index, const char *text, size_t length)
{
    return locker_storage_status_from_sqlite(
        sqlite3_bind_text(stmt, index, text, (int)length, SQLITE_TRANSIENT));
}

/* ---------- 事务 ---------- */

locker_status_t locker_storage_begin_write(locker_repo_t *repo)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (repo->in_transaction) {
        return LOCKER_STATUS_CONFLICT;
    }
    locker_status_t status = exec_sql(repo->db, "BEGIN IMMEDIATE");
    if (status == LOCKER_STATUS_OK) {
        repo->in_transaction = true;
    }
    return status;
}

locker_status_t locker_storage_begin_read(locker_repo_t *repo)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (repo->in_transaction) {
        return LOCKER_STATUS_CONFLICT;
    }
    locker_status_t status = exec_sql(repo->db, "BEGIN");
    if (status == LOCKER_STATUS_OK) {
        repo->in_transaction = true;
    }
    return status;
}

locker_status_t locker_storage_commit(locker_repo_t *repo)
{
    if (repo == NULL || !repo->in_transaction) {
        return LOCKER_STATUS_INVALID;
    }
    int rc = sqlite3_exec(repo->db, "COMMIT", NULL, NULL, NULL);
    repo->in_transaction = false;
    if (rc != SQLITE_OK) {
        /* COMMIT 失败时事务可能仍然活着；显式回滚，避免连接停在未决事务里。 */
        (void)sqlite3_exec(repo->db, "ROLLBACK", NULL, NULL, NULL);
        return locker_storage_status_from_sqlite(rc);
    }
    return LOCKER_STATUS_OK;
}

void locker_storage_rollback(locker_repo_t *repo)
{
    if (repo == NULL || !repo->in_transaction) {
        return;
    }
    (void)sqlite3_exec(repo->db, "ROLLBACK", NULL, NULL, NULL);
    repo->in_transaction = false;
}

/* ---------- meta ---------- */

static locker_status_t db_read_meta(sqlite3 *db, const char *name, char *out_text, size_t out_size)
{
    if (db == NULL || name == NULL || out_text == NULL || out_size < 2u) {
        return LOCKER_STATUS_INVALID;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(db, "SELECT value FROM meta WHERE name = ?1", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = LOCKER_STATUS_IO_ERROR;
    if (bind_text(stmt, 1, name, strlen(name)) == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const unsigned char *text = sqlite3_column_text(stmt, 0);
            int bytes = sqlite3_column_bytes(stmt, 0);
            if (text != NULL && bytes > 0 && (size_t)bytes < out_size) {
                memcpy(out_text, text, (size_t)bytes);
                out_text[bytes] = '\0';
                status = LOCKER_STATUS_OK;
            }
        } else if (rc == SQLITE_DONE) {
            status = LOCKER_STATUS_NOT_READY;
        } else {
            status = locker_storage_status_from_sqlite(rc);
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

static locker_status_t db_table_exists(sqlite3 *db, const char *name, bool *out_exists)
{
    if (db == NULL || name == NULL || out_exists == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_exists = false;
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = LOCKER_STATUS_IO_ERROR;
    if (bind_text(stmt, 1, name, strlen(name)) == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            *out_exists = true;
            status = LOCKER_STATUS_OK;
        } else if (rc == SQLITE_DONE) {
            status = LOCKER_STATUS_OK;
        } else {
            status = locker_storage_status_from_sqlite(rc);
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

/* 任意类型的既有对象（table/view/index/trigger）：初始化判定必须看「目标里有没有
   用户对象」，而不是「有没有 documents 表」。审计 round-2 B3：只按 documents 表判断，
   一个同名 VIEW 会让 DDL 的 CREATE TABLE IF NOT EXISTS 静默跳过，首次 open 假成功。 */
static locker_status_t db_object_exists(sqlite3 *db, const char *name, bool *out_exists)
{
    if (db == NULL || name == NULL || out_exists == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_exists = false;
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT 1 FROM sqlite_master WHERE name = ?1", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = LOCKER_STATUS_IO_ERROR;
    if (bind_text(stmt, 1, name, strlen(name)) == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            *out_exists = true;
            status = LOCKER_STATUS_OK;
        } else if (rc == SQLITE_DONE) {
            status = LOCKER_STATUS_OK;
        } else {
            status = locker_storage_status_from_sqlite(rc);
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

/* 非 SQLite 内部对象（sqlite_*）的数量：用来区分「真正空的目标」和
   「已经有过初始化痕迹的库」。审计 B3/round-2 B3：既包括表也包括视图，
   否则只有 meta 表或只有 documents 视图的库会被当成新库覆盖。 */
static locker_status_t db_user_object_count(sqlite3 *db, uint64_t *out_count)
{
    if (db == NULL || out_count == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_count = 0u;
    sqlite3_stmt *stmt = NULL;
    /* 必须用 GLOB 而不是 LIKE：LIKE 的 '_' 是「任意单字符」通配符，
       'sqlite_%' 会连用户自己建的 sqlitex_audit 一起排除掉，于是有痕迹的库被当成
       空目标补建（审计 round-3 B-1）。GLOB 的 '_' 是字面量，'sqlite_*' 才是
       「内部保留前缀」的准确表达。 */
    locker_status_t status = prepare_stmt(
        db, "SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out_count = (uint64_t)sqlite3_column_int64(stmt, 0);
        status = LOCKER_STATUS_OK;
    } else if (rc == SQLITE_DONE) {
        status = LOCKER_STATUS_IO_ERROR;
    } else {
        status = locker_storage_status_from_sqlite(rc);
    }
    sqlite3_finalize(stmt);
    return status;
}

static locker_status_t db_read_revision(sqlite3 *db, uint64_t *out_revision)
{
    if (out_revision == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    char text[LOCKER_STORAGE_U64_TEXT_MAX];
    locker_status_t status = db_read_meta(db, "snapshot_revision", text, sizeof(text));
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (!locker_storage_u64_parse(text, strlen(text), out_revision)) {
        return LOCKER_STATUS_IO_ERROR;
    }
    return LOCKER_STATUS_OK;
}

static locker_status_t db_write_revision(sqlite3 *db, uint64_t revision)
{
    char text[LOCKER_STORAGE_U64_TEXT_MAX];
    if (!locker_storage_u64_format(revision, text, sizeof(text))) {
        return LOCKER_STATUS_INVALID;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(db, "UPDATE meta SET value = ?1 WHERE name = 'snapshot_revision'", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = bind_text(stmt, 1, text, strlen(text));
    if (status == LOCKER_STATUS_OK) {
        status = step_write(stmt);
        if (status == LOCKER_STATUS_OK && sqlite3_changes(db) != 1) {
            status = LOCKER_STATUS_IO_ERROR;
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

locker_status_t locker_storage_read_revision(locker_repo_t *repo, uint64_t *out_revision)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return db_read_revision(repo->db, out_revision);
}

locker_status_t locker_storage_write_revision(locker_repo_t *repo, uint64_t revision)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return db_write_revision(repo->db, revision);
}

/* ---------- 文档读写 ---------- */

static locker_status_t db_document_exists(sqlite3 *db, const char *collection, const char *key,
                                          bool *out_exists)
{
    if (db == NULL || collection == NULL || key == NULL || out_exists == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_exists = false;
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT 1 FROM documents WHERE collection = ?1 AND key = ?2", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = bind_text(stmt, 1, collection, strlen(collection));
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 2, key, strlen(key));
    }
    if (status == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            *out_exists = true;
            status = LOCKER_STATUS_OK;
        } else if (rc == SQLITE_DONE) {
            status = LOCKER_STATUS_OK;
        } else {
            status = locker_storage_status_from_sqlite(rc);
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

locker_status_t locker_storage_document_exists(locker_repo_t *repo, const char *collection,
                                               const char *key, bool *out_exists)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return db_document_exists(repo->db, collection, key, out_exists);
}

static locker_status_t db_put_document(sqlite3 *db, const char *collection, const char *key,
                                       const cJSON *value)
{
    if (db == NULL || collection == NULL || key == NULL || value == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    char *text = cJSON_PrintUnformatted(value);
    if (text == NULL) {
        return LOCKER_STATUS_NO_SPACE;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db,
        "INSERT INTO documents(collection, key, value_json) VALUES (?1, ?2, ?3) "
        "ON CONFLICT(collection, key) DO UPDATE SET value_json = excluded.value_json",
        &stmt);
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 1, collection, strlen(collection));
    }
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 2, key, strlen(key));
    }
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 3, text, strlen(text));
    }
    if (status == LOCKER_STATUS_OK) {
        status = step_write(stmt);
    }
    if (status == LOCKER_STATUS_OK) {
        /* 审计 round-6 B-1 第二道防线：UPSERT 的合法结果必然是「恰好改动 1 行」。
           预存的 BEFORE INSERT 触发器里的 RAISE(IGNORE) 会让语句**静默跳过**、既不报错
           也不写入（SQLite 官方语义：放弃该语句但保留已有变化），step 仍返回 DONE。
           只认 SQLITE_DONE 就会把「什么都没写」当成功并继续提交 revision。 */
        if (sqlite3_changes(db) != 1) {
            status = LOCKER_STATUS_IO_ERROR;
        }
    }
    sqlite3_finalize(stmt);
    cJSON_free(text);
    return status;
}

locker_status_t locker_storage_put_document(locker_repo_t *repo, const char *collection, const char *key,
                                            const cJSON *value)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return db_put_document(repo->db, collection, key, value);
}

static locker_status_t db_delete_document(sqlite3 *db, const char *collection, const char *key)
{
    if (db == NULL || collection == NULL || key == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "DELETE FROM documents WHERE collection = ?1 AND key = ?2", &stmt);
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 1, collection, strlen(collection));
    }
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 2, key, strlen(key));
    }
    bool existed = false;
    if (status == LOCKER_STATUS_OK) {
        status = db_document_exists(db, collection, key, &existed);
    }
    if (status == LOCKER_STATUS_OK) {
        status = step_write(stmt);
    }
    if (status == LOCKER_STATUS_OK && existed && sqlite3_changes(db) != 1) {
        /* 审计 round-6 B-1 第二道防线：要删的行确实存在，但 DELETE 一行都没动，
           说明有触发器把这条语句静默吞掉了（RAISE(IGNORE)）。删除不存在的 key
           仍然是合法的空操作，所以只在「本来存在」时才要求 changes == 1。 */
        status = LOCKER_STATUS_IO_ERROR;
    }
    sqlite3_finalize(stmt);
    return status;
}

locker_status_t locker_storage_delete_document(locker_repo_t *repo, const char *collection, const char *key)
{
    if (repo == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    return db_delete_document(repo->db, collection, key);
}

/* ---------- JSON 辅助 ---------- */

cJSON *locker_storage_parse_json(const char *json, size_t length, bool require_complete)
{
    if (json == NULL || length == 0u) {
        return NULL;
    }
    if (locker_storage_json_text_has_embedded_nul(json, length)) {
        /* 嵌入 NUL 在 cJSON 里变成 U+0000，下游 strlen 会截断（审计 B5）。 */
        return NULL;
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, 0);
    if (root == NULL) {
        return NULL;
    }
    if (require_complete) {
        size_t consumed = 0u;
        if (end != NULL && end >= json) {
            consumed = (size_t)(end - json);
        }
        if (consumed > length) {
            cJSON_Delete(root);
            return NULL;
        }
        for (size_t i = consumed; i < length; ++i) {
            char c = json[i];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                cJSON_Delete(root);
                return NULL;
            }
        }
    }
    return root;
}

bool locker_storage_json_u64(const cJSON *node, uint64_t *out_value)
{
    if (!cJSON_IsString(node) || out_value == NULL) {
        return false;
    }
    return locker_storage_u64_parse(node->valuestring, strlen(node->valuestring), out_value);
}

static const char *json_string_field(const cJSON *object, const char *name)
{
    if (object == NULL || name == NULL) {
        return NULL;
    }
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(node)) {
        return NULL;
    }
    return node->valuestring;
}

static bool add_u64_string(cJSON *object, const char *name, uint64_t value)
{
    char text[LOCKER_STORAGE_U64_TEXT_MAX];
    if (object == NULL || name == NULL || !locker_storage_u64_format(value, text, sizeof(text))) {
        return false;
    }
    return cJSON_AddStringToObject(object, name, text) != NULL;
}

static bool add_uuid_string(cJSON *object, const char *name, const locker_uuid_t *uuid)
{
    char text[LOCKER_STORAGE_UUID_TEXT_MAX];
    if (object == NULL || name == NULL || uuid == NULL) {
        return false;
    }
    if (locker_uuid_format(uuid, text, sizeof(text)) != LOCKER_STATUS_OK) {
        return false;
    }
    return cJSON_AddStringToObject(object, name, text) != NULL;
}

static cJSON *build_event_key_json(const locker_event_key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    size_t device_length = 0u;
    if (!locker_storage_text_length(key->device_id, sizeof(key->device_id), &device_length)) {
        return NULL;
    }
    if (device_length != 24u) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    bool ok = cJSON_AddStringToObject(root, "device_id", key->device_id) != NULL &&
              add_uuid_string(root, "journal_epoch", &key->journal_epoch) &&
              add_u64_string(root, "boot_id", key->boot_id) &&
              add_u64_string(root, "event_seq", key->event_seq);
    if (!ok) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static bool event_key_from_json(const cJSON *node, locker_event_key_t *out)
{
    if (!cJSON_IsObject(node) || out == NULL) {
        return false;
    }
    const char *device_id = json_string_field(node, "device_id");
    const char *journal_epoch = json_string_field(node, "journal_epoch");
    if (device_id == NULL || journal_epoch == NULL || strlen(device_id) != 24u) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->device_id, device_id, 24u);
    out->device_id[24] = '\0';
    if (locker_uuid_parse(journal_epoch, strlen(journal_epoch), &out->journal_epoch) != LOCKER_STATUS_OK) {
        return false;
    }
    if (!locker_storage_json_u64(cJSON_GetObjectItemCaseSensitive(node, "boot_id"), &out->boot_id)) {
        return false;
    }
    if (!locker_storage_json_u64(cJSON_GetObjectItemCaseSensitive(node, "event_seq"), &out->event_seq)) {
        return false;
    }
    return true;
}

static cJSON *build_document_json(const char *collection, const char *key, const cJSON *value)
{
    if (collection == NULL || key == NULL || value == NULL) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    if (cJSON_AddStringToObject(root, "collection", collection) == NULL ||
        cJSON_AddStringToObject(root, "key", key) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON *copy = cJSON_Duplicate(value, 1);
    if (copy == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "value", copy);
    return root;
}

static locker_status_t write_json_result(cJSON *root, char *out, size_t out_size, size_t *out_length)
{
    if (root == NULL || out == NULL || out_length == NULL || out_size == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    char *text = cJSON_PrintUnformatted(root);
    if (text == NULL) {
        return LOCKER_STATUS_NO_SPACE;
    }
    size_t length = strlen(text);
    locker_status_t status = LOCKER_STATUS_OK;
    if (length + 1u > out_size) {
        status = LOCKER_STATUS_NO_SPACE;
    } else {
        memcpy(out, text, length + 1u);
        *out_length = length;
    }
    cJSON_free(text);
    return status;
}

/* ---------- RepositoryChange 解析与校验 ---------- */

cJSON *locker_storage_parse_change(locker_repo_t *repo, const char *json, size_t length,
                                   locker_status_t *out_status)
{
    if (out_status == NULL) {
        return NULL;
    }
    *out_status = LOCKER_STATUS_INVALID;
    if (repo == NULL || repo->schema == NULL) {
        return NULL;
    }
    cJSON *root = locker_storage_parse_json(json, length, true);
    if (root == NULL) {
        return NULL;
    }
    if (!locker_storage_schema_validate_def_value(repo->schema, "RepositoryChange", root)) {
        cJSON_Delete(root);
        return NULL;
    }
    *out_status = LOCKER_STATUS_OK;
    return root;
}

locker_status_t locker_storage_apply_change(locker_repo_t *repo, const cJSON *change)
{
    if (repo == NULL || change == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    uint64_t expected = 0u;
    uint64_t next = 0u;
    if (!locker_storage_json_u64(cJSON_GetObjectItemCaseSensitive(change, "expected_snapshot_revision"),
                                 &expected) ||
        !locker_storage_json_u64(cJSON_GetObjectItemCaseSensitive(change, "new_snapshot_revision"), &next)) {
        return LOCKER_STATUS_INVALID;
    }
    if (expected == UINT64_MAX || next != expected + 1u) {
        return LOCKER_STATUS_INVALID;
    }

    uint64_t current = 0u;
    locker_status_t status = db_read_revision(repo->db, &current);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (current != expected) {
        return LOCKER_STATUS_CONFLICT;
    }

    const cJSON *checks = cJSON_GetObjectItemCaseSensitive(change, "checks");
    if (!cJSON_IsArray(checks)) {
        return LOCKER_STATUS_INVALID;
    }
    const cJSON *check = NULL;
    cJSON_ArrayForEach(check, checks)
    {
        const char *collection = json_string_field(check, "collection");
        const char *key = json_string_field(check, "key");
        const char *condition = json_string_field(check, "condition");
        if (collection == NULL || key == NULL || condition == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        bool want_exists = false;
        if (strcmp(condition, "EXISTS") == 0) {
            want_exists = true;
        } else if (strcmp(condition, "ABSENT") != 0) {
            return LOCKER_STATUS_INVALID;
        }
        bool exists = false;
        status = db_document_exists(repo->db, collection, key, &exists);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
        if (exists != want_exists) {
            return LOCKER_STATUS_CONFLICT;
        }
    }

    const cJSON *mutations = cJSON_GetObjectItemCaseSensitive(change, "mutations");
    if (!cJSON_IsArray(mutations)) {
        return LOCKER_STATUS_INVALID;
    }
    const cJSON *mutation = NULL;
    cJSON_ArrayForEach(mutation, mutations)
    {
        const char *op = json_string_field(mutation, "op");
        if (op == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        if (strcmp(op, "DELETE") == 0) {
            const char *collection = json_string_field(mutation, "collection");
            const char *key = json_string_field(mutation, "key");
            if (collection == NULL || key == NULL) {
                return LOCKER_STATUS_INVALID;
            }
            status = db_delete_document(repo->db, collection, key);
            if (status != LOCKER_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (strcmp(op, "PUT") != 0) {
            return LOCKER_STATUS_INVALID;
        }
        const cJSON *document = cJSON_GetObjectItemCaseSensitive(mutation, "document");
        const char *collection = json_string_field(document, "collection");
        const char *key = json_string_field(document, "key");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(document, "value");
        if (collection == NULL || key == NULL || value == NULL) {
            return LOCKER_STATUS_INVALID;
        }
        size_t collection_index = 0u;
        if (!locker_storage_schema_collection_lookup(collection, strlen(collection), &collection_index)) {
            return LOCKER_STATUS_INVALID;
        }
        /* x-rules 02：value 必须满足该 collection 对应记录，而不是 anyOf 任一分支。 */
        if (!locker_storage_schema_validate_document_value(repo->schema, collection_index, value)) {
            return LOCKER_STATUS_INVALID;
        }
        status = db_put_document(repo->db, collection, key, value);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }

    return db_write_revision(repo->db, next);
}

/* ---------- 设备事件记录 ---------- */

static const char *door_state_name(locker_door_state_t door)
{
    switch (door) {
    case LOCKER_DOOR_STATE_UNKNOWN:
        return "UNKNOWN";
    case LOCKER_DOOR_STATE_CLOSED:
        return "CLOSED";
    case LOCKER_DOOR_STATE_OPEN:
        return "OPEN";
    default:
        return NULL;
    }
}

static const char *item_state_name(locker_item_state_t item)
{
    switch (item) {
    case LOCKER_ITEM_STATE_UNKNOWN:
        return "UNKNOWN";
    case LOCKER_ITEM_STATE_ABSENT:
        return "ABSENT";
    case LOCKER_ITEM_STATE_PRESENT:
        return "PRESENT";
    default:
        return NULL;
    }
}

/* card_uid -> 小写 hex；长度只允许 0/4/7/10（uart-v1.md:44），剩余字节必须为 0。 */
static bool card_uid_to_hex(const locker_device_event_t *event, char *out, size_t out_size)
{
    static const char k_hex[] = "0123456789abcdef";
    size_t length = event->card_uid_len;
    if (length != 0u && length != 4u && length != 7u && length != 10u) {
        return false;
    }
    if (length == 0u) {
        out[0] = '\0';
        return true;
    }
    if (out_size < length * 2u + 1u) {
        return false;
    }
    for (size_t i = length; i < sizeof(event->card_uid); ++i) {
        if (event->card_uid[i] != 0u) {
            return false;
        }
    }
    for (size_t i = 0; i < length; ++i) {
        out[i * 2u] = k_hex[(event->card_uid[i] >> 4) & 0x0fu];
        out[i * 2u + 1u] = k_hex[event->card_uid[i] & 0x0fu];
    }
    out[length * 2u] = '\0';
    return true;
}

cJSON *locker_storage_build_event_record(const locker_device_event_t *event)
{
    if (event == NULL) {
        return NULL;
    }
    if (event->event_type < 1u || event->event_type > 15u) {
        return NULL;
    }
    if (event->reason > 14u || (event->flags & (uint16_t)~0x001fu) != 0u) {
        return NULL;
    }
    const char *door = door_state_name(event->door);
    const char *item = item_state_name(event->item);
    if (door == NULL || item == NULL) {
        return NULL;
    }
    char card_uid_hex[21];
    if (!card_uid_to_hex(event, card_uid_hex, sizeof(card_uid_hex))) {
        return NULL;
    }
    bool supply_valid = (event->flags & 0x0001u) != 0u;
    if (supply_valid) {
        if (event->supply_mv > 100000u) {
            return NULL;
        }
    } else if (event->supply_mv != UINT32_MAX) {
        /* uart-v1.md:42：未知必须用 0xffffffff 且 SUPPLY_VALID=0，不能混用 0。 */
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON *key = build_event_key_json(&event->key);
    if (key == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "key", key);
    bool ok = add_u64_string(root, "mono_ms", event->mono_ms) &&
              add_u64_string(root, "physical_revision", event->physical_revision) &&
              cJSON_AddNumberToObject(root, "event_type", (double)event->event_type) != NULL &&
              cJSON_AddNumberToObject(root, "reason", (double)event->reason) != NULL &&
              cJSON_AddNumberToObject(root, "flags", (double)event->flags) != NULL &&
              cJSON_AddStringToObject(root, "door", door) != NULL &&
              cJSON_AddStringToObject(root, "item", item) != NULL &&
              add_u64_string(root, "detail", event->detail);
    if (!ok) {
        cJSON_Delete(root);
        return NULL;
    }
    if (locker_uuid_is_nil(&event->operation_id)) {
        cJSON_AddNullToObject(root, "operation_id");
    } else if (!add_uuid_string(root, "operation_id", &event->operation_id)) {
        cJSON_Delete(root);
        return NULL;
    }
    if (locker_uuid_is_nil(&event->command_id)) {
        cJSON_AddNullToObject(root, "command_id");
    } else if (!add_uuid_string(root, "command_id", &event->command_id)) {
        cJSON_Delete(root);
        return NULL;
    }
    if (supply_valid) {
        cJSON_AddNumberToObject(root, "supply_mv", (double)event->supply_mv);
    } else {
        cJSON_AddNullToObject(root, "supply_mv");
    }
    if (card_uid_hex[0] == '\0') {
        cJSON_AddNullToObject(root, "card_uid_hex");
    } else if (cJSON_AddStringToObject(root, "card_uid_hex", card_uid_hex) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/* ---------- 游标记录 ---------- */

static cJSON *build_cursor_record(const locker_event_key_t *through, bool continuity_ok)
{
    if (through == NULL) {
        return NULL;
    }
    size_t device_length = 0u;
    if (!locker_storage_text_length(through->device_id, sizeof(through->device_id), &device_length) ||
        device_length != 24u) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON *key = build_event_key_json(through);
    if (key == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    if (cJSON_AddStringToObject(root, "device_id", through->device_id) == NULL ||
        !add_uuid_string(root, "journal_epoch", &through->journal_epoch)) {
        cJSON_Delete(key);
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "through", key);
    cJSON_AddBoolToObject(root, "continuity_ok", continuity_ok);
    return root;
}

/* ---------- 分页读取 ---------- */

/* 一页 documents 数组允许占用的序列化字节（审计 B4）。
 *
 * 做法：先按最坏情况序列化一次「空页信封」（snapshot_revision 取 UINT64_MAX、
 * recovery 时带最长集合名、next_after_key 为 null），把信封长度从输出缓冲里扣掉，
 * 再给 next_after_key 预留「最长 key 全部按 \uXXXX 转义」的空间。
 * 剩下的才是 documents 数组内容可用的字节，collect_page 据此选记录，
 * 保证「选中的记录 + 续页键」一定能装进调用方的缓冲。 */
static size_t page_documents_budget(const char *collection, bool recovery_page, size_t out_size)
{
    const size_t reserved_next_key = LOCKER_STORAGE_KEY_MAX * 6u + 8u;
    if (out_size <= reserved_next_key + 2u) {
        return 0u;
    }
    cJSON *probe = cJSON_CreateObject();
    if (probe == NULL) {
        return 0u;
    }
    bool built = cJSON_AddNumberToObject(probe, "schema_version", 1.0) != NULL &&
                 cJSON_AddStringToObject(probe, "snapshot_revision", "18446744073709551615") != NULL;
    if (built && recovery_page) {
        built = cJSON_AddStringToObject(probe, "collection", collection) != NULL;
    }
    if (built) {
        built = cJSON_AddItemToObject(probe, "documents", cJSON_CreateArray()) != 0;
    }
    if (built) {
        built = cJSON_AddNullToObject(probe, "next_after_key") != NULL;
    }
    if (!built) {
        cJSON_Delete(probe);
        return 0u;
    }
    char *text = cJSON_PrintUnformatted(probe);
    cJSON_Delete(probe);
    if (text == NULL) {
        return 0u;
    }
    const size_t envelope = strlen(text);
    cJSON_free(text);
    /* envelope 里 "[]" 占 2 字节，被 documents 内容替换掉。 */
    const size_t fixed = envelope - 2u;
    if (out_size - 1u <= fixed + reserved_next_key) {
        return 0u;
    }
    return out_size - 1u - fixed - reserved_next_key;
}

/* 分页按「最终序列化字节」而不是 key+value 之和来限流（审计 B4）。
 * docs_budget 是 documents 数组本身允许占用的序列化字节数（含条目间逗号），
 * 由 run_query 用真实信封长度算出来；这里只负责不超过它。 */
static locker_status_t collect_page(sqlite3 *db, const char *collection, const char *after_key,
                                    uint16_t limit, size_t docs_budget, cJSON *documents,
                                    bool *out_has_next, char *out_next, size_t out_next_size)
{
    if (db == NULL || collection == NULL || documents == NULL || out_has_next == NULL ||
        out_next == NULL || out_next_size < 2u || limit == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    *out_has_next = false;
    out_next[0] = '\0';

    const char *sql = NULL;
    if (after_key != NULL && after_key[0] != '\0') {
        sql = "SELECT key, value_json FROM documents WHERE collection = ?1 AND key > ?2 "
              "ORDER BY key LIMIT ?3";
    } else {
        sql = "SELECT key, value_json FROM documents WHERE collection = ?1 ORDER BY key LIMIT ?2";
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(db, sql, &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    int rows_requested = (int)limit + 1;
    status = bind_text(stmt, 1, collection, strlen(collection));
    if (status == LOCKER_STATUS_OK && after_key != NULL && after_key[0] != '\0') {
        status = bind_text(stmt, 2, after_key, strlen(after_key));
    }
    if (status == LOCKER_STATUS_OK) {
        int index = (after_key != NULL && after_key[0] != '\0') ? 3 : 2;
        status = locker_storage_status_from_sqlite(sqlite3_bind_int(stmt, index, rows_requested));
    }

    size_t total_bytes = 0u;
    size_t included = 0u;
    bool more = false;
    char last_key[LOCKER_STORAGE_KEY_MAX + 1u];
    last_key[0] = '\0';

    while (status == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            status = locker_storage_status_from_sqlite(rc);
            break;
        }
        if (included >= (size_t)limit) {
            more = true;
            break;
        }
        const unsigned char *key_text = sqlite3_column_text(stmt, 0);
        const unsigned char *value_text = sqlite3_column_text(stmt, 1);
        int key_bytes = sqlite3_column_bytes(stmt, 0);
        int value_bytes = sqlite3_column_bytes(stmt, 1);
        if (key_text == NULL || value_text == NULL || key_bytes <= 0 || value_bytes <= 0 ||
            (size_t)key_bytes > LOCKER_STORAGE_KEY_MAX) {
            status = LOCKER_STATUS_IO_ERROR;
            break;
        }
        cJSON *value = locker_storage_parse_json((const char *)value_text, (size_t)value_bytes, true);
        if (value == NULL) {
            status = LOCKER_STATUS_IO_ERROR;
            break;
        }
        char key_copy[LOCKER_STORAGE_KEY_MAX + 1u];
        memcpy(key_copy, key_text, (size_t)key_bytes);
        key_copy[key_bytes] = '\0';
        cJSON *document = build_document_json(collection, key_copy, value);
        cJSON_Delete(value);
        if (document == NULL) {
            status = LOCKER_STATUS_NO_SPACE;
            break;
        }
        /* 按这条文档序列化后的真实长度计预算：key/value 里可能有需要转义的字符
           （中文、引号、反斜杠），把它们按原始字节数算会低估（审计 B4）。 */
        char *document_text = cJSON_PrintUnformatted(document);
        if (document_text == NULL) {
            cJSON_Delete(document);
            status = LOCKER_STATUS_NO_SPACE;
            break;
        }
        const size_t document_bytes = strlen(document_text);
        cJSON_free(document_text);
        const size_t separator = (included == 0u) ? 0u : 1u;
        if (total_bytes + separator + document_bytes > docs_budget) {
            /* 装不下下一条：保留已选中的有效页，交给调用方带续页键返回。 */
            cJSON_Delete(document);
            more = true;
            break;
        }
        cJSON_AddItemToArray(documents, document);
        total_bytes += separator + document_bytes;
        included += 1u;
        memcpy(last_key, key_copy, (size_t)key_bytes + 1u);
    }
    sqlite3_finalize(stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    if (more && included == 0u) {
        /* 单条记录就超过页字节上限，不能静默截断。 */
        return LOCKER_STATUS_NO_SPACE;
    }
    if (more) {
        size_t length = strlen(last_key);
        if (length + 1u > out_next_size) {
            return LOCKER_STATUS_NO_SPACE;
        }
        memcpy(out_next, last_key, length + 1u);
        *out_has_next = true;
    }
    return LOCKER_STATUS_OK;
}

/* ---------- 查询 ---------- */

static bool collection_lookup(const char *collection, size_t *out_index)
{
    if (collection == NULL || out_index == NULL) {
        return false;
    }
    return locker_storage_schema_collection_lookup(collection, strlen(collection), out_index);
}

static locker_status_t run_query(locker_repo_t *repo, const locker_repo_query_t *query,
                                 bool recovery_page, char *out_json, size_t out_size,
                                 size_t *out_length, uint64_t *out_revision)
{
    if (repo == NULL || query == NULL || out_json == NULL || out_length == NULL || out_revision == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (query->schema_version != LOCKER_STORAGE_REPO_SCHEMA_VERSION) {
        return LOCKER_STATUS_INVALID;
    }
    size_t collection_length = 0u;
    size_t key_length = 0u;
    size_t after_length = 0u;
    if (!locker_storage_text_length(query->collection, sizeof(query->collection), &collection_length) ||
        collection_length == 0u) {
        return LOCKER_STATUS_INVALID;
    }
    if (!locker_storage_text_length(query->key, sizeof(query->key), &key_length) ||
        !locker_storage_text_length(query->after_key, sizeof(query->after_key), &after_length)) {
        return LOCKER_STATUS_INVALID;
    }
    if (key_length > LOCKER_STORAGE_KEY_MAX || after_length > LOCKER_STORAGE_KEY_MAX) {
        return LOCKER_STATUS_INVALID;
    }
    size_t collection_index = 0u;
    if (!collection_lookup(query->collection, &collection_index)) {
        return LOCKER_STATUS_INVALID;
    }
    (void)collection_index;
    if (recovery_page && key_length != 0u) {
        /* 恢复按 collection 分页，不按单键取（x-rules 10）。 */
        return LOCKER_STATUS_INVALID;
    }
    if (key_length != 0u && (after_length != 0u || query->limit != 1u)) {
        /* 按键分支与分页分支互斥：key 分支的 after_key 必须为空、limit 必须为 1（x-rules 09）。 */
        return LOCKER_STATUS_INVALID;
    }
    if (key_length == 0u && (query->limit == 0u || query->limit > LOCKER_STORAGE_PAGE_ROWS_MAX)) {
        /* 分页 limit 必须在 1..100，显式拒绝而不是交给 schema 兜底。 */
        return LOCKER_STATUS_INVALID;
    }

    cJSON *request = cJSON_CreateObject();
    if (request == NULL) {
        return LOCKER_STATUS_NO_SPACE;
    }
    bool ok = cJSON_AddNumberToObject(request, "schema_version", 1.0) != NULL &&
              cJSON_AddStringToObject(request, "collection", query->collection) != NULL;
    if (ok) {
        if (key_length != 0u) {
            ok = cJSON_AddStringToObject(request, "key", query->key) != NULL &&
                 cJSON_AddNullToObject(request, "after_key") != NULL &&
                 cJSON_AddNumberToObject(request, "limit", 1.0) != NULL;
        } else {
            ok = cJSON_AddNullToObject(request, "key") != NULL;
            if (ok && after_length != 0u) {
                ok = cJSON_AddStringToObject(request, "after_key", query->after_key) != NULL;
            } else if (ok) {
                ok = cJSON_AddNullToObject(request, "after_key") != NULL;
            }
            if (ok) {
                ok = cJSON_AddNumberToObject(request, "limit", (double)query->limit) != NULL;
            }
        }
    }
    if (!ok) {
        cJSON_Delete(request);
        return LOCKER_STATUS_NO_SPACE;
    }
    if (!locker_storage_schema_validate_def_value(repo->schema, "RepositoryQuery", request)) {
        cJSON_Delete(request);
        return LOCKER_STATUS_INVALID;
    }

    locker_status_t status = locker_storage_begin_read(repo);
    if (status != LOCKER_STATUS_OK) {
        cJSON_Delete(request);
        return status;
    }
    uint64_t revision = 0u;
    status = db_read_revision(repo->db, &revision);
    cJSON *documents = NULL;
    if (status == LOCKER_STATUS_OK) {
        documents = cJSON_CreateArray();
        if (documents == NULL) {
            status = LOCKER_STATUS_NO_SPACE;
        }
    }
    char next_key[LOCKER_STORAGE_KEY_MAX + 1u];
    next_key[0] = '\0';
    bool has_next = false;
    if (status == LOCKER_STATUS_OK) {
        if (key_length != 0u) {
            sqlite3_stmt *stmt = NULL;
            status = prepare_stmt(
                repo->db, "SELECT value_json FROM documents WHERE collection = ?1 AND key = ?2", &stmt);
            if (status == LOCKER_STATUS_OK) {
                status = bind_text(stmt, 1, query->collection, collection_length);
            }
            if (status == LOCKER_STATUS_OK) {
                status = bind_text(stmt, 2, query->key, key_length);
            }
            if (status == LOCKER_STATUS_OK) {
                int rc = sqlite3_step(stmt);
                if (rc == SQLITE_ROW) {
                    const unsigned char *value_text = sqlite3_column_text(stmt, 0);
                    int value_bytes = sqlite3_column_bytes(stmt, 0);
                    if (value_text == NULL || value_bytes <= 0) {
                        status = LOCKER_STATUS_IO_ERROR;
                    } else {
                        cJSON *value =
                            locker_storage_parse_json((const char *)value_text, (size_t)value_bytes, true);
                        if (value == NULL) {
                            status = LOCKER_STATUS_IO_ERROR;
                        } else {
                            cJSON *document = build_document_json(query->collection, query->key, value);
                            cJSON_Delete(value);
                            if (document == NULL) {
                                status = LOCKER_STATUS_NO_SPACE;
                            } else {
                                cJSON_AddItemToArray(documents, document);
                            }
                        }
                    }
                } else if (rc != SQLITE_DONE) {
                    status = locker_storage_status_from_sqlite(rc);
                }
            }
            sqlite3_finalize(stmt);
        } else {
            status = collect_page(repo->db, query->collection, query->after_key, query->limit,
                                  page_documents_budget(query->collection, recovery_page, out_size),
                                  documents, &has_next, next_key, sizeof(next_key));
        }
    }
    if (status == LOCKER_STATUS_OK) {
        status = locker_storage_commit(repo);
    } else {
        locker_storage_rollback(repo);
    }
    cJSON_Delete(request);

    cJSON *result = NULL;
    if (status == LOCKER_STATUS_OK) {
        result = cJSON_CreateObject();
        if (result == NULL) {
            status = LOCKER_STATUS_NO_SPACE;
        }
    }
    if (status == LOCKER_STATUS_OK) {
        bool built = cJSON_AddNumberToObject(result, "schema_version", 1.0) != NULL &&
                     add_u64_string(result, "snapshot_revision", revision);
        if (built && recovery_page) {
            built = cJSON_AddStringToObject(result, "collection", query->collection) != NULL;
        }
        if (built) {
            cJSON_AddItemToObject(result, "documents", documents);
            documents = NULL;
            if (has_next) {
                built = cJSON_AddStringToObject(result, "next_after_key", next_key) != NULL;
            } else {
                built = cJSON_AddNullToObject(result, "next_after_key") != NULL;
            }
        }
        if (!built) {
            status = LOCKER_STATUS_NO_SPACE;
        }
    }
    if (status == LOCKER_STATUS_OK) {
        const char *def_name = recovery_page ? "RepositoryRecoveryPage" : "RepositoryQueryResult";
        if (!locker_storage_schema_validate_def_value(repo->schema, def_name, result)) {
            status = LOCKER_STATUS_IO_ERROR;
        } else {
            status = write_json_result(result, out_json, out_size, out_length);
        }
    }
    if (status == LOCKER_STATUS_OK) {
        *out_revision = revision;
    }
    cJSON_Delete(documents);
    cJSON_Delete(result);
    return status;
}

locker_status_t locker_repo_query(locker_repo_t *repo, const locker_repo_query_t *query,
                                  locker_repo_result_t *result)
{
    if (repo == NULL || query == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    result->length = 0u;
    result->json[0] = '\0';
    result->snapshot_revision = 0u;
    uint64_t revision = 0u;
    locker_status_t status = run_query(repo, query, false, result->json, sizeof(result->json),
                                       &result->length, &revision);
    if (status == LOCKER_STATUS_OK) {
        result->snapshot_revision = revision;
    }
    return status;
}

locker_status_t locker_repo_load_recovery(locker_repo_t *repo, const locker_repo_query_t *query,
                                          locker_recovery_t *result)
{
    if (repo == NULL || query == NULL || result == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    result->length = 0u;
    result->json[0] = '\0';
    result->snapshot_revision = 0u;
    uint64_t revision = 0u;
    locker_status_t status = run_query(repo, query, true, result->json, sizeof(result->json),
                                       &result->length, &revision);
    if (status == LOCKER_STATUS_OK) {
        result->snapshot_revision = revision;
    }
    return status;
}

/* ---------- 事务性提交 ---------- */

locker_status_t locker_repo_commit_change(locker_repo_t *repo, const locker_change_t *change)
{
    if (repo == NULL || change == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (change->length == 0u || change->length > sizeof(change->json)) {
        return LOCKER_STATUS_INVALID;
    }
    locker_status_t status = LOCKER_STATUS_INVALID;
    cJSON *root = locker_storage_parse_change(repo, change->json, change->length, &status);
    if (root == NULL) {
        return status;
    }
    status = locker_storage_begin_write(repo);
    if (status == LOCKER_STATUS_OK) {
        status = locker_storage_apply_change(repo, root);
        if (status == LOCKER_STATUS_OK) {
            status = locker_storage_commit(repo);
        } else {
            locker_storage_rollback(repo);
        }
    }
    cJSON_Delete(root);
    return status;
}

/* ---------- 事件导入 ---------- */

/* 同一设备同一 journal epoch 才算同一条日志；不同日志的事件键互不可比。 */
static bool event_key_same_log(const locker_event_key_t *left, const locker_event_key_t *right)
{
    return memcmp(left->device_id, right->device_id, sizeof(left->device_id)) == 0 &&
           locker_uuid_equal(&left->journal_epoch, &right->journal_epoch);
}

/* 事件键结构（审计 round-2 B2）：uart-v1.md:38 规定 (0,0) 是「开始/空日志」哨兵，
   「禁止仅其中一个为 0」。半零键（boot=0,seq>0 或 boot>0,seq=0）既不是哨兵也不是
   合法事件位置，必须在比较之前就拒掉，否则它会以 device/epoch 参与数值比较、
   冒充日志边界。返回 true 表示结构合法（全零哨兵或两个分量都非零）。 */
static bool event_key_structure_ok(const locker_event_key_t *key)
{
    if (key == NULL) {
        return false;
    }
    bool boot_is_zero = key->boot_id == 0u;
    bool seq_is_zero = key->event_seq == 0u;
    return boot_is_zero == seq_is_zero;
}

/* 页内相邻（审计 B1）：同 boot 内 event_seq 必须严格 +1；换 boot 必须 boot_id
   递增且从 seq=1 开始。boot 之间允许跳号（那次启动可能没有落任何事件），
   但同一 boot 内缺号说明页不完整，不能当成连续历史。 */
static bool event_keys_adjacent(const locker_event_key_t *previous, const locker_event_key_t *next)
{
    if (previous->boot_id == next->boot_id) {
        if (previous->event_seq == UINT64_MAX) {
            return false;
        }
        return next->event_seq == previous->event_seq + 1u;
    }
    if (next->boot_id <= previous->boot_id) {
        return false;
    }
    return next->event_seq == 1u;
}

/* 游标能否从 stored 严格连续推进到 first_new（x-rules 11、uart-v1.md:372）。
 * event_seq 在每个 boot 内从 1 开始，boot_id 单调递增。
 * 这里要求「恰好接上」而不是「不超过 stored+1」：允许跳号会把缺口写进水位。
 * 与 event_keys_adjacent 用同一规则：同 boot 必须 seq+1；换 boot 必须 boot 递增
 * 且 seq=1。boot 号本身可以跳（那次启动没有落任何事件），跳过 boot 号不是事件缺口，
 * 用 boot_id == stored+1 会把它误判成缺口而永久卡住。 */
static bool cursor_is_contiguous(const locker_event_key_t *stored, const locker_event_key_t *first_new)
{
    if (stored == NULL || first_new == NULL) {
        return false;
    }
    return event_keys_adjacent(stored, first_new);
}

static locker_status_t read_cursor_record(locker_repo_t *repo, const char *cursor_key, bool *out_exists,
                                          locker_event_key_t *out_through, bool *out_continuity_ok)
{
    if (repo == NULL || cursor_key == NULL || out_exists == NULL || out_through == NULL ||
        out_continuity_ok == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_exists = false;
    memset(out_through, 0, sizeof(*out_through));
    *out_continuity_ok = false;
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        repo->db, "SELECT value_json FROM documents WHERE collection = ?1 AND key = ?2", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = bind_text(stmt, 1, LOCKER_STORAGE_COLLECTION_IMPORT_CURSORS,
                       strlen(LOCKER_STORAGE_COLLECTION_IMPORT_CURSORS));
    if (status == LOCKER_STATUS_OK) {
        status = bind_text(stmt, 2, cursor_key, strlen(cursor_key));
    }
    if (status == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const unsigned char *value_text = sqlite3_column_text(stmt, 0);
            int value_bytes = sqlite3_column_bytes(stmt, 0);
            if (value_text == NULL || value_bytes <= 0) {
                status = LOCKER_STATUS_IO_ERROR;
            } else {
                cJSON *record =
                    locker_storage_parse_json((const char *)value_text, (size_t)value_bytes, true);
                if (record == NULL) {
                    status = LOCKER_STATUS_IO_ERROR;
                } else {
                    if (!event_key_from_json(cJSON_GetObjectItemCaseSensitive(record, "through"),
                                             out_through)) {
                        status = LOCKER_STATUS_IO_ERROR;
                    } else {
                        const cJSON *continuity =
                            cJSON_GetObjectItemCaseSensitive(record, "continuity_ok");
                        if (!cJSON_IsBool(continuity)) {
                            status = LOCKER_STATUS_IO_ERROR;
                        } else {
                            *out_continuity_ok = cJSON_IsTrue(continuity) != 0;
                            *out_exists = true;
                            status = LOCKER_STATUS_OK;
                        }
                    }
                    cJSON_Delete(record);
                }
            }
        } else if (rc != SQLITE_DONE) {
            status = locker_storage_status_from_sqlite(rc);
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

locker_status_t locker_repo_import_events(locker_repo_t *repo, const locker_event_batch_t *batch,
                                          const locker_change_t *projection_change,
                                          locker_event_key_t *committed_cursor)
{
    if (repo == NULL || batch == NULL || committed_cursor == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (batch->count == 0u || batch->count > LOCKER_STORAGE_EVENT_BATCH_MAX) {
        return LOCKER_STATUS_INVALID;
    }

    locker_event_key_t first_key;
    locker_event_key_t last_key;
    memset(&first_key, 0, sizeof(first_key));
    memset(&last_key, 0, sizeof(last_key));
    for (size_t i = 0; i < (size_t)batch->count; ++i) {
        const locker_device_event_t *event = &batch->events[i];
        size_t device_length = 0u;
        if (!locker_storage_text_length(event->key.device_id, sizeof(event->key.device_id),
                                        &device_length) ||
            device_length != 24u) {
            return LOCKER_STATUS_INVALID;
        }
        if (locker_storage_event_key_is_zero(&event->key)) {
            return LOCKER_STATUS_INVALID;
        }
        if (!event_key_structure_ok(&event->key)) {
            /* 半零键（例如 boot=2,seq=0）不是事件位置，禁止进入比较（round-2 B2）。 */
            return LOCKER_STATUS_INVALID;
        }
        if (i == 0u) {
            first_key = event->key;
        } else {
            if (memcmp(first_key.device_id, event->key.device_id, sizeof(first_key.device_id)) != 0 ||
                !locker_uuid_equal(&first_key.journal_epoch, &event->key.journal_epoch)) {
                /* 一页事件必须来自同一设备、同一日志 epoch。 */
                return LOCKER_STATUS_INVALID;
            }
            if (locker_storage_event_key_compare(&last_key, &event->key) >= 0) {
                /* 页内事件必须严格递增，乱序或重复都拒绝（不猜先后）。 */
                return LOCKER_STATUS_INVALID;
            }
            if (!event_keys_adjacent(&last_key, &event->key)) {
                /* 同一 boot 内缺号说明这一页不是连续历史（审计 B1-A），
                   不能把它当成「已经接上」写进水位。 */
                return LOCKER_STATUS_INVALID;
            }
        }
        last_key = event->key;
    }

    /* 批次边界结构校验（审计 B2）：先确认这页属于哪条日志、水位到哪，再做数值比较。
       原来只在 last_committed 非零时比大小，零游标或另一设备的游标都能绕过上界。 */
    if (locker_storage_event_key_is_zero(&batch->last_committed)) {
        /* 非空页必须带 MCU 已提交水位，否则无法判断是否越过日志尾。 */
        return LOCKER_STATUS_INVALID;
    }
    if (!event_key_structure_ok(&batch->last_committed) ||
        !event_key_structure_ok(&batch->first_available) ||
        !event_key_structure_ok(&batch->next_after)) {
        /* 边界游标同样只能是「全零哨兵」或「boot/seq 都非零」；半零游标不能
           以 device/epoch 参与数值比较来冒充日志边界（round-2 B2）。 */
        return LOCKER_STATUS_INVALID;
    }
    if (!event_key_same_log(&first_key, &batch->last_committed)) {
        return LOCKER_STATUS_INVALID;
    }
    if (locker_storage_event_key_compare(&last_key, &batch->last_committed) > 0) {
        return LOCKER_STATUS_INVALID;
    }
    if (locker_storage_event_key_is_zero(&batch->first_available)) {
        /* 非空页不能把 first_available 整体清零：uart-v1.md:370 要求带缺口的
           响应同时给出 first_available，空日志哨兵不能扩充成「未知首边界」
           （round-2 B2）。 */
        return LOCKER_STATUS_INVALID;
    }
    if (!event_key_same_log(&first_key, &batch->first_available)) {
        return LOCKER_STATUS_INVALID;
    }
    if (locker_storage_event_key_compare(&first_key, &batch->first_available) < 0) {
        /* 页里出现早于 MCU 声明「最早可用」的记录：这一帧不可信。 */
        return LOCKER_STATUS_INVALID;
    }
    if (!event_key_same_log(&first_key, &batch->next_after) ||
        locker_storage_event_key_compare(&last_key, &batch->next_after) != 0) {
        /* uart-v1.md:368：有记录时 next_after 就是本页最后一条记录。 */
        return LOCKER_STATUS_INVALID;
    }

    char cursor_key[LOCKER_STORAGE_IMPORT_CURSOR_KEY_TEXT_MAX];
    if (!locker_storage_import_cursor_key_build(&first_key, cursor_key, sizeof(cursor_key))) {
        return LOCKER_STATUS_INVALID;
    }
    size_t events_index = 0u;
    if (!locker_storage_schema_collection_lookup(LOCKER_STORAGE_COLLECTION_DEVICE_EVENTS,
                                                strlen(LOCKER_STORAGE_COLLECTION_DEVICE_EVENTS),
                                                &events_index)) {
        return LOCKER_STATUS_INVALID;
    }

    locker_status_t status = locker_storage_begin_write(repo);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }

    bool stored_exists = false;
    locker_event_key_t stored_through;
    bool stored_continuity = false;
    status = read_cursor_record(repo, cursor_key, &stored_exists, &stored_through, &stored_continuity);

    size_t new_count = 0u;
    locker_event_key_t first_new;
    memset(&first_new, 0, sizeof(first_new));
    for (size_t i = 0; status == LOCKER_STATUS_OK && i < (size_t)batch->count; ++i) {
        const locker_device_event_t *event = &batch->events[i];
        cJSON *record = locker_storage_build_event_record(event);
        if (record == NULL) {
            status = LOCKER_STATUS_INVALID;
            break;
        }
        if (!locker_storage_schema_validate_document_value(repo->schema, events_index, record)) {
            cJSON_Delete(record);
            status = LOCKER_STATUS_INVALID;
            break;
        }
        char event_key[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
        if (!locker_storage_event_key_build(&event->key, event_key, sizeof(event_key))) {
            cJSON_Delete(record);
            status = LOCKER_STATUS_INVALID;
            break;
        }
        bool exists = false;
        status = db_document_exists(repo->db, LOCKER_STORAGE_COLLECTION_DEVICE_EVENTS, event_key, &exists);
        if (status == LOCKER_STATUS_OK && !exists) {
            if (new_count == 0u) {
                first_new = event->key;
            }
            status = db_put_document(repo->db, LOCKER_STORAGE_COLLECTION_DEVICE_EVENTS, event_key, record);
            if (status == LOCKER_STATUS_OK) {
                new_count += 1u;
            }
        }
        cJSON_Delete(record);
    }

    if (status != LOCKER_STATUS_OK) {
        locker_storage_rollback(repo);
        return status;
    }

    if (new_count == 0u) {
        /* 全部是重复事实：不重复改投影、不推进游标（x-rules 11）。
           但「本页之前有已知缺口」是必须锁存的新事实：即使一条事件都不用写，
           也要把 continuity_ok 降级落库，否则缺口会被后续正常页洗白
           （审计 round-2 B1）。 */
        if (!batch->gap_before_page) {
            locker_storage_rollback(repo);
            if (stored_exists) {
                *committed_cursor = stored_through;
            } else {
                *committed_cursor = last_key;
            }
            return LOCKER_STATUS_OK;
        }
        if (!stored_exists) {
            /* 事件在库里、水位记录却不在：状态不一致，不能凭这一页重建水位。 */
            locker_storage_rollback(repo);
            return LOCKER_STATUS_CONFLICT;
        }
        if (!stored_continuity) {
            /* 缺口早已锁存，重复页不改变任何事实。 */
            locker_storage_rollback(repo);
            *committed_cursor = stored_through;
            return LOCKER_STATUS_OK;
        }
        cJSON *latch_record = build_cursor_record(&stored_through, false);
        if (latch_record == NULL) {
            locker_storage_rollback(repo);
            return LOCKER_STATUS_NO_SPACE;
        }
        locker_status_t latch_status =
            db_put_document(repo->db, LOCKER_STORAGE_COLLECTION_IMPORT_CURSORS, cursor_key, latch_record);
        cJSON_Delete(latch_record);
        if (latch_status != LOCKER_STATUS_OK) {
            locker_storage_rollback(repo);
            return latch_status;
        }
        latch_status = locker_storage_commit(repo);
        if (latch_status != LOCKER_STATUS_OK) {
            locker_storage_rollback(repo);
            return latch_status;
        }
        *committed_cursor = stored_through;
        return LOCKER_STATUS_OK;
    }

    bool continuity_ok = true;
    if (stored_exists) {
        if (!cursor_is_contiguous(&stored_through, &first_new)) {
            /* 缺口不能被游标越过：整页回滚，Core 必须先补取缺口。 */
            locker_storage_rollback(repo);
            return LOCKER_STATUS_CONFLICT;
        }
        if (locker_storage_event_key_compare(&first_new, &stored_through) <= 0) {
            /* 同 boot 的历史回填：水位不能倒退，也不能用旧记录覆盖新水位。 */
            locker_storage_rollback(repo);
            return LOCKER_STATUS_CONFLICT;
        }
        continuity_ok = stored_continuity;
    } else {
        /* 首次为这个 (device, epoch) 建游标：从日志开头接入才算连续。 */
        continuity_ok = !batch->gap_before_page && first_new.boot_id == 1u && first_new.event_seq == 1u;
    }
    if (batch->gap_before_page) {
        /* 本页之前存在已知缺口：即使这一页内部连续，也不能声称完整历史
           （uart-v1.md:370「这不证明完整历史」）。缺口不可被后续页洗白。 */
        continuity_ok = false;
    }

    if (projection_change != NULL) {
        if (projection_change->length == 0u || projection_change->length > sizeof(projection_change->json)) {
            locker_storage_rollback(repo);
            return LOCKER_STATUS_INVALID;
        }
        locker_status_t parse_status = LOCKER_STATUS_INVALID;
        cJSON *projection =
            locker_storage_parse_change(repo, projection_change->json, projection_change->length,
                                        &parse_status);
        if (projection == NULL) {
            locker_storage_rollback(repo);
            return parse_status;
        }
        status = locker_storage_apply_change(repo, projection);
        cJSON_Delete(projection);
        if (status != LOCKER_STATUS_OK) {
            locker_storage_rollback(repo);
            return status;
        }
    }

    cJSON *cursor_record = build_cursor_record(&last_key, continuity_ok);
    if (cursor_record == NULL) {
        locker_storage_rollback(repo);
        return LOCKER_STATUS_NO_SPACE;
    }
    status = db_put_document(repo->db, LOCKER_STORAGE_COLLECTION_IMPORT_CURSORS, cursor_key, cursor_record);
    cJSON_Delete(cursor_record);
    if (status != LOCKER_STATUS_OK) {
        locker_storage_rollback(repo);
        return status;
    }
    status = locker_storage_commit(repo);
    if (status != LOCKER_STATUS_OK) {
        locker_storage_rollback(repo);
        return status;
    }
    *committed_cursor = last_key;
    return LOCKER_STATUS_OK;
}

/* ---------- 打开与关闭 ---------- */

static locker_status_t verify_schema_metadata(sqlite3 *db)
{
    char version_text[LOCKER_STORAGE_U64_TEXT_MAX];
    locker_status_t status = db_read_meta(db, "schema_version", version_text, sizeof(version_text));
    if (status != LOCKER_STATUS_OK) {
        return status == LOCKER_STATUS_NOT_READY ? LOCKER_STATUS_CONFLICT : status;
    }
    uint64_t version = 0u;
    if (!locker_storage_u64_parse(version_text, strlen(version_text), &version)) {
        return LOCKER_STATUS_CONFLICT;
    }
    if (version != (uint64_t)LOCKER_STORAGE_REPO_SCHEMA_VERSION) {
        /* 不兼容的 schema 明确报错，不自动删库/重建（x-rules 13）。 */
        return LOCKER_STATUS_CONFLICT;
    }
    sqlite3_stmt *stmt = NULL;
    status = prepare_stmt(db, "SELECT 1 FROM migrations WHERE version = ?1", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = locker_storage_status_from_sqlite(sqlite3_bind_int(stmt, 1, (int)LOCKER_STORAGE_REPO_SCHEMA_VERSION));
    if (status == LOCKER_STATUS_OK) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            status = LOCKER_STATUS_OK;
        } else if (rc == SQLITE_DONE) {
            status = LOCKER_STATUS_CONFLICT;
        } else {
            status = locker_storage_status_from_sqlite(rc);
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

/* 已初始化库的必要表结构（审计 B3、round-3 B-2）。
 *
 * 只查「三张表名存在」不够：把 documents 换成别的列（甚至只剩两列）、或者去掉
 * (collection,key) 的主键/唯一约束，open 仍会返回 OK，第一次 query 才报
 * "no such column"，第一次 PUT 才报 "ON CONFLICT clause does not match any
 * PRIMARY KEY or UNIQUE constraint"。这里把列结构、主键位置、键唯一索引的比较
 * 规则一起核，任一处不符即 CONFLICT，且不修改库。 */
typedef struct {
    const char *name;
    const char *type;
    bool not_null;
    int pk_position; /* 复合主键内的 1-based 位置；0 表示不属于主键 */
} expected_column_t;

typedef struct {
    const char *table;
    const expected_column_t *columns;
    size_t column_count;
    /* 必须存在的唯一索引列（按顺序）；NULL 表示该表的主键是 rowid 别名，
       SQLite 不建独立索引，只靠 table_info 的 pk 位置核验。 */
    const char *const *key_columns;
    size_t key_column_count;
} expected_table_t;

static bool ascii_text_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    size_t index = 0u;
    for (;;) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
        if (a == '\0') {
            return true;
        }
        ++index;
    }
}

static locker_status_t verify_table_columns(sqlite3 *db, const expected_table_t *spec)
{
    if (db == NULL || spec == NULL || spec->table == NULL || spec->columns == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT name, type, [notnull], pk, hidden FROM pragma_table_xinfo(?1) ORDER BY cid", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = bind_text(stmt, 1, spec->table, strlen(spec->table));
    size_t index = 0u;
    if (status == LOCKER_STATUS_OK) {
        for (;;) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                if (index >= spec->column_count) {
                    /* 多出来的列同样说明不是我们的 schema。 */
                    status = LOCKER_STATUS_CONFLICT;
                    break;
                }
                const expected_column_t *column = &spec->columns[index];
                const char *name = (const char *)sqlite3_column_text(stmt, 0);
                const char *type = (const char *)sqlite3_column_text(stmt, 1);
                int not_null = sqlite3_column_int(stmt, 2);
                int pk_position = sqlite3_column_int(stmt, 3);
                int hidden = sqlite3_column_int(stmt, 4);
                /* 审计 round-6 B-2：pragma_table_info 会**省略**生成列（VIRTUAL/STORED）和
                   其它隐藏列，于是「官方三列 + 末尾追加一个 NOT NULL 生成列」的库在
                   table_info 里看起来仍是标准三列，open 放行、第一次合法 PUT 才报
                   NOT NULL constraint failed。table_xinfo 才报全，hidden != 0 一律拒绝。 */
                if (hidden != 0) {
                    status = LOCKER_STATUS_CONFLICT;
                    break;
                }
                if (!ascii_text_equal(name, column->name) || !ascii_text_equal(type, column->type) ||
                    (not_null != 0) != column->not_null || pk_position != column->pk_position) {
                    status = LOCKER_STATUS_CONFLICT;
                    break;
                }
                /* 列的默认排序规则必须也是 BINARY：唯一索引可以显式写成 BINARY 而列本身是
                   NOCASE，这样索引唯一性是二进制、但 WHERE/ORDER BY 用列排序规则，大小写不同的
                   两个 key 会同时被匹配、被一起 DELETE（审计 round-4）。pragma_table_info 不报
                   排序规则，只能用 sqlite3_table_column_metadata 取（table_xinfo 同样不报）。 */
                const char *collation = NULL;
                if (sqlite3_table_column_metadata(db, "main", spec->table, column->name, NULL,
                                                  &collation, NULL, NULL, NULL) != SQLITE_OK ||
                    collation == NULL || !ascii_text_equal(collation, "BINARY")) {
                    status = LOCKER_STATUS_CONFLICT;
                    break;
                }
                ++index;
            } else if (rc == SQLITE_DONE) {
                break;
            } else {
                status = locker_storage_status_from_sqlite(rc);
                break;
            }
        }
    }
    if (status == LOCKER_STATUS_OK && index != spec->column_count) {
        status = LOCKER_STATUS_CONFLICT;
    }
    sqlite3_finalize(stmt);
    return status;
}

/* 一个索引是否「恰好覆盖期望的键列、升序、BINARY 比较」。
   NOCASE 之类的排序规则会让 key 的唯一性与 ORDER BY 语义都变，必须拒绝。 */
static locker_status_t index_covers_key(sqlite3 *db, const char *index_name,
                                        const expected_table_t *spec, bool *out_matches)
{
    if (db == NULL || index_name == NULL || spec == NULL || out_matches == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out_matches = false;
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT name, [desc], coll FROM pragma_index_xinfo(?1) WHERE [key] = 1 ORDER BY seqno",
        &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = bind_text(stmt, 1, index_name, strlen(index_name));
    bool matches = true;
    size_t index = 0u;
    if (status == LOCKER_STATUS_OK) {
        for (;;) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                if (index >= spec->key_column_count) {
                    matches = false;
                    ++index;
                    continue;
                }
                const char *name = (const char *)sqlite3_column_text(stmt, 0);
                int desc = sqlite3_column_int(stmt, 1);
                const char *collation = (const char *)sqlite3_column_text(stmt, 2);
                if (!ascii_text_equal(name, spec->key_columns[index]) || desc != 0 ||
                    !ascii_text_equal(collation, "BINARY")) {
                    matches = false;
                }
                ++index;
            } else if (rc == SQLITE_DONE) {
                break;
            } else {
                status = locker_storage_status_from_sqlite(rc);
                break;
            }
        }
    }
    if (status == LOCKER_STATUS_OK && matches && index == spec->key_column_count) {
        *out_matches = true;
    }
    sqlite3_finalize(stmt);
    return status;
}

/* 表上每一个唯一约束都必须是「恰好覆盖键列、升序、BINARY」，并且至少存在一个。
   不能用「找到任一匹配索引就放行」：主键可以显式写成 COLLATE NOCASE，同时再建一个
   BINARY 唯一索引，这样键唯一性仍按 NOCASE 判、字节不同的两个 key 被同一个约束拒绝
   （审计 round-4 B-1）。反过来，额外挂在别的列上的唯一索引也会改变可接受键集合，
   同样按不兼容处理。 */
static locker_status_t verify_key_index(sqlite3 *db, const expected_table_t *spec)
{
    if (spec->key_column_count == 0u) {
        return LOCKER_STATUS_OK;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT name, [unique], partial FROM pragma_index_list(?1)", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = bind_text(stmt, 1, spec->table, strlen(spec->table));
    size_t matched_unique = 0u;
    if (status == LOCKER_STATUS_OK) {
        for (;;) {
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                const char *index_name = (const char *)sqlite3_column_text(stmt, 0);
                int is_unique = sqlite3_column_int(stmt, 1);
                int is_partial = sqlite3_column_int(stmt, 2);
                if (index_name == NULL || is_unique == 0) {
                    continue;
                }
                /* 部分唯一索引（CREATE UNIQUE INDEX ... WHERE ...）只在满足谓词的行上生效，
                   所以它不是全局唯一约束、不能代替主键；但它确实会拒绝一部分本来合法的写入
                   （例如 WHERE collection='audits' 的 partial unique 会让两条合法审计记录冲突）。
                   未知的部分约束一律按不兼容处理（审计 round-5 B-1）。 */
                if (is_partial != 0) {
                    status = LOCKER_STATUS_CONFLICT;
                    break;
                }
                bool matches = false;
                locker_status_t index_status = index_covers_key(db, index_name, spec, &matches);
                if (index_status != LOCKER_STATUS_OK) {
                    status = index_status;
                    break;
                }
                if (!matches) {
                    status = LOCKER_STATUS_CONFLICT;
                    break;
                }
                ++matched_unique;
            } else if (rc == SQLITE_DONE) {
                break;
            } else {
                status = locker_storage_status_from_sqlite(rc);
                break;
            }
        }
    }
    if (status == LOCKER_STATUS_OK && matched_unique == 0u) {
        status = LOCKER_STATUS_CONFLICT;
    }
    sqlite3_finalize(stmt);
    return status;
}

/* 已初始化库的必要表结构：缺表、换列、丢主键/唯一约束、键排序规则不是 BINARY
   都不能当成可用库（审计 B3、round-3 B-2）。 */
/* 定义在 verify_schema_tables 之后（它依赖 ascii_text_equal 之后的位置约定），
   这里先声明以便在同一函数末尾调用。 */
static locker_status_t verify_official_objects(sqlite3 *db);
static locker_status_t verify_official_ddl(sqlite3 *db);

static locker_status_t verify_schema_tables(sqlite3 *db)
{
    static const expected_column_t k_meta_columns[] = {
        {"name", "TEXT", true, 1},
        {"value", "TEXT", true, 0},
    };
    static const char *const k_meta_key[] = {"name"};
    static const expected_column_t k_migrations_columns[] = {
        {"version", "INTEGER", true, 1},
        {"applied_at", "TEXT", true, 0},
        {"checksum", "TEXT", true, 0},
    };
    static const expected_column_t k_documents_columns[] = {
        {"collection", "TEXT", true, 1},
        {"key", "TEXT", true, 2},
        {"value_json", "TEXT", true, 0},
    };
    static const char *const k_documents_key[] = {"collection", "key"};
    static const expected_table_t k_tables[] = {
        {"meta", k_meta_columns, 2u, k_meta_key, 1u},
        {"migrations", k_migrations_columns, 3u, NULL, 0u},
        {"documents", k_documents_columns, 3u, k_documents_key, 2u},
    };
    for (size_t index = 0u; index < sizeof(k_tables) / sizeof(k_tables[0]); ++index) {
        locker_status_t status = verify_table_columns(db, &k_tables[index]);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
        status = verify_key_index(db, &k_tables[index]);
        if (status != LOCKER_STATUS_OK) {
            return status;
        }
    }
    /* 三张表结构对还不够：库里不能有官方 DDL 之外的任何对象（round-6 B-1）。 */
    locker_status_t status = verify_official_objects(db);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    /* 对象清单也对，还要求三张表的 CREATE TABLE 文本与唯一 schema-v1.sql 逐字节一致：
       CHECK / FOREIGN KEY 这类表内约束不产生额外对象、也不改变列与索引视图（round-7 B-1）。 */
    return verify_official_ddl(db);
}

/* 审计 round-6 B-1：列/索引/metadata 都对，也不代表这个库只会按我们的 DDL 行事。
   预存一个 BEFORE INSERT ... RAISE(IGNORE) 触发器，就能让合法的 UPSERT 静默不生效，
   而 open 与 commit 都返回成功。这里的判据是最小且完备的：官方 schema-v1.sql 建出的
   库里 sqlite_* 之外**恰好**只有三张表 meta/migrations/documents，没有任何用户索引、
   视图或触发器；多一个就是被改过的库，一律 CONFLICT，不做「只认已知危险类型」的
   黑名单猜测。 */
static locker_status_t verify_official_objects(sqlite3 *db)
{
    if (db == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    static const char *const k_tables[] = {"meta", "migrations", "documents"};
    const size_t table_count = sizeof(k_tables) / sizeof(k_tables[0]);
    bool seen[sizeof(k_tables) / sizeof(k_tables[0])] = {false, false, false};
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT type, name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    size_t object_count = 0u;
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            status = locker_storage_status_from_sqlite(rc);
            break;
        }
        const char *type = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        ++object_count;
        if (!ascii_text_equal(type, "table")) {
            /* 用户索引/视图/触发器：官方 DDL 不会产生，全部拒绝。 */
            status = LOCKER_STATUS_CONFLICT;
            break;
        }
        size_t index = 0u;
        for (; index < table_count; ++index) {
            if (ascii_text_equal(name, k_tables[index])) {
                if (seen[index]) {
                    status = LOCKER_STATUS_CONFLICT;
                }
                seen[index] = true;
                break;
            }
        }
        if (status != LOCKER_STATUS_OK) {
            break;
        }
        if (index == table_count) {
            status = LOCKER_STATUS_CONFLICT;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (status == LOCKER_STATUS_OK && object_count != table_count) {
        status = LOCKER_STATUS_CONFLICT;
    }
    return status;
}

/* ---------- 官方 DDL 文本比对（round-7 B-1） ---------- */

/* 标识符字符：只用于判断表名后面是否还有别的标识符字符，
   避免 meta 匹配到 metadata。手写而不依赖 ctype 的区域设置。 */
static bool ddl_identifier_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* 从唯一 schema-v1.sql 里重建某张表的期望 CREATE TABLE 文本。
   sqlite_master.sql 的形态是：官方 DDL 去掉 IF NOT EXISTS、去掉结尾分号，
   其余空白/换行/引号原样保留；所以这里按同样规则拼出文本再逐字节比较。
   扫描过程跟踪括号深度与引号状态，字符串/标识符里的括号和分号不算结构。
   返回 false 表示真源自身不含这张表，或文本放不进调用方缓冲。 */
static bool expected_table_ddl(const char *schema, size_t schema_size, const char *table,
                               char *out, size_t out_size, size_t *out_length)
{
    static const char k_prefix[] = "CREATE TABLE ";
    static const char k_needle[] = "CREATE TABLE IF NOT EXISTS ";
    const size_t prefix_length = sizeof(k_prefix) - 1u;
    const size_t needle_length = sizeof(k_needle) - 1u;
    const size_t table_length = strlen(table);
    if (schema == NULL || table == NULL || out == NULL || out_length == NULL) {
        return false;
    }
    size_t offset = 0u;
    while (offset + needle_length <= schema_size) {
        if (memcmp(schema + offset, k_needle, needle_length) != 0) {
            ++offset;
            continue;
        }
        size_t name_start = offset + needle_length;
        while (name_start < schema_size && (schema[name_start] == ' ' || schema[name_start] == '\t' ||
                                            schema[name_start] == '\r' || schema[name_start] == '\n')) {
            ++name_start;
        }
        if (name_start + table_length >= schema_size ||
            memcmp(schema + name_start, table, table_length) != 0) {
            ++offset;
            continue;
        }
        size_t cursor = name_start + table_length;
        if (cursor < schema_size && ddl_identifier_char(schema[cursor])) {
            ++offset;
            continue;
        }
        int depth = 0;
        char quote = '\0';
        bool closed = false;
        size_t scan = cursor;
        while (scan < schema_size) {
            const char c = schema[scan];
            if (quote != '\0') {
                const char closing = (quote == '[') ? ']' : quote;
                if (c == closing) {
                    /* 双写引号（两个单引号、两个双引号等）是转义，不结束引用。 */
                    if (scan + 1u < schema_size && schema[scan + 1u] == closing) {
                        scan += 2u;
                        continue;
                    }
                    quote = '\0';
                }
                ++scan;
                continue;
            }
            if (c == '\'' || c == '"' || c == '`' || c == '[') {
                quote = c;
                ++scan;
                continue;
            }
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth > 0) {
                    --depth;
                }
            } else if (c == ';' && depth == 0) {
                closed = true;
                break;
            }
            ++scan;
        }
        if (!closed) {
            return false;
        }
        const size_t body_length = scan - name_start;
        if (prefix_length + body_length >= out_size) {
            return false;
        }
        memcpy(out, k_prefix, prefix_length);
        memcpy(out + prefix_length, schema + name_start, body_length);
        *out_length = prefix_length + body_length;
        return true;
    }
    return false;
}

/* 三张表的 CREATE TABLE 文本必须与唯一 schema-v1.sql 逐字节一致。
   CHECK 约束和 FOREIGN KEY 都是表内约束：既不产生额外 sqlite_master 对象，
   也不改变 pragma 的列/索引视图，只能靠 DDL 文本本身识别（round-7 B-1）。
   一旦不一致就 CONFLICT，不做「只认已知危险约束」的黑名单猜测。 */
static locker_status_t verify_official_ddl(sqlite3 *db)
{
    static const char *const k_tables[] = {"meta", "migrations", "documents"};
    const size_t table_count = sizeof(k_tables) / sizeof(k_tables[0]);
    char expected[1024];
    if (db == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(
        db, "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?1", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    size_t index = 0u;
    for (; index < table_count; ++index) {
        size_t expected_length = 0u;
        if (!expected_table_ddl(locker_storage_schema_sql, locker_storage_schema_sql_size,
                                k_tables[index], expected, sizeof(expected), &expected_length)) {
            /* 真源里找不到这张表，说明嵌入的 DDL 本身不对，不能接纳任何库。 */
            status = LOCKER_STATUS_CONFLICT;
            break;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        status = bind_text(stmt, 1, k_tables[index], strlen(k_tables[index]));
        if (status != LOCKER_STATUS_OK) {
            break;
        }
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            status = (rc == SQLITE_DONE) ? LOCKER_STATUS_CONFLICT
                                         : locker_storage_status_from_sqlite(rc);
            break;
        }
        const unsigned char *stored = sqlite3_column_text(stmt, 0);
        if (stored == NULL || strlen((const char *)stored) != expected_length ||
            memcmp(stored, expected, expected_length) != 0) {
            status = LOCKER_STATUS_CONFLICT;
            break;
        }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            status = (rc == SQLITE_ROW) ? LOCKER_STATUS_CONFLICT
                                        : locker_storage_status_from_sqlite(rc);
            break;
        }
    }
    sqlite3_finalize(stmt);
    return status;
}

static locker_status_t run_quick_check(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    locker_status_t status = prepare_stmt(db, "PRAGMA quick_check", &stmt);
    if (status != LOCKER_STATUS_OK) {
        return status;
    }
    status = LOCKER_STATUS_IO_ERROR;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text != NULL && strcmp((const char *)text, "ok") == 0) {
            status = LOCKER_STATUS_OK;
        }
    } else if (rc == SQLITE_DONE) {
        status = LOCKER_STATUS_IO_ERROR;
    } else {
        status = locker_storage_status_from_sqlite(rc);
    }
    sqlite3_finalize(stmt);
    return status;
}

locker_status_t locker_repo_open(const locker_repo_config_t *config, locker_repo_t **out)
{
    if (out == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    *out = NULL;
    if (config == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (config->schema_version != LOCKER_STORAGE_REPO_SCHEMA_VERSION) {
        return LOCKER_STATUS_INVALID;
    }
    size_t path_length = 0u;
    if (!locker_storage_text_length(config->path, sizeof(config->path), &path_length) ||
        path_length == 0u) {
        return LOCKER_STATUS_INVALID;
    }

    int flags = SQLITE_OPEN_READWRITE;
    if (config->initialize_empty) {
        flags |= SQLITE_OPEN_CREATE;
    }
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(config->path, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close_v2(db);
        }
        /* 未要求初始化却打不开：多半是文件不存在，属「尚未初始化」而不是损坏。 */
        if (!config->initialize_empty && (rc & 0xff) == SQLITE_CANTOPEN) {
            return LOCKER_STATUS_NOT_READY;
        }
        return LOCKER_STATUS_IO_ERROR;
    }

    locker_status_t status = LOCKER_STATUS_OK;
    if (sqlite3_busy_timeout(db, LOCKER_STORAGE_BUSY_TIMEOUT_MS) != SQLITE_OK) {
        status = LOCKER_STATUS_IO_ERROR;
    }
    if (status == LOCKER_STATUS_OK) {
        status = exec_sql(db, "PRAGMA foreign_keys = ON");
    }
    if (status == LOCKER_STATUS_OK) {
        status = exec_sql(db, "PRAGMA journal_mode = WAL");
    }
    if (status == LOCKER_STATUS_OK) {
        /* synchronous=FULL：提交返回前日志已落盘（卡固定约束）。 */
        status = exec_sql(db, "PRAGMA synchronous = FULL");
    }

    /* 初始化判定（审计 B3、round-2 B3）：区分三种情况——
       1) 真正空的目标（没有任何用户对象，表/视图/索引/触发器都算）：允许
          initialize_empty 建库；
       2) 已完整初始化的库（三张表齐全且无同名视图）：只校验元数据，不动 DDL；
       3) 有痕迹但结构不全（只有 meta、迁移失败中途退出、documents 是视图）：
          一律 CONFLICT，绝不用 CREATE TABLE IF NOT EXISTS + INSERT OR IGNORE
          把半成品「补齐」成可用库。 */
    bool documents_is_table = false;
    bool documents_any_object = false;
    uint64_t user_objects = 0u;
    if (status == LOCKER_STATUS_OK) {
        status = db_table_exists(db, "documents", &documents_is_table);
    }
    if (status == LOCKER_STATUS_OK) {
        status = db_object_exists(db, "documents", &documents_any_object);
    }
    if (status == LOCKER_STATUS_OK) {
        status = db_user_object_count(db, &user_objects);
    }
    if (status == LOCKER_STATUS_OK && documents_any_object && !documents_is_table) {
        /* documents 这个名字已被视图等非表对象占用：DDL 的 IF NOT EXISTS 会静默
           跳过它，建出来的库第一次能开、第二次必然 CONFLICT（round-2 B3）。 */
        status = LOCKER_STATUS_CONFLICT;
    }
    if (status == LOCKER_STATUS_OK && !documents_is_table && user_objects != 0u) {
        status = LOCKER_STATUS_CONFLICT;
    }
    if (status == LOCKER_STATUS_OK && !documents_is_table) {
        if (!config->initialize_empty) {
            status = LOCKER_STATUS_NOT_READY;
        } else {
            /* 首次 DDL + 元数据必须同一个事务：任何一步失败整体回滚，
               不能留下「表建了但版本没写」的半初始化库（审计 B3）。 */
            status = exec_sql(db, "BEGIN IMMEDIATE");
            if (status == LOCKER_STATUS_OK) {
                status = exec_sql(db, locker_storage_schema_sql);
            }
            if (status == LOCKER_STATUS_OK) {
                /* 提交前核验三张表真的建出来了：视图占位等情况不能靠 DDL 的
                   IF NOT EXISTS 蒙混过去（round-2 B3）。 */
                status = verify_schema_tables(db);
            }
            if (status == LOCKER_STATUS_OK) {
                status = verify_schema_metadata(db);
            }
            if (status == LOCKER_STATUS_OK) {
                status = exec_sql(db, "COMMIT");
            } else {
                (void)exec_sql(db, "ROLLBACK");
            }
        }
    } else if (status == LOCKER_STATUS_OK) {
        status = verify_schema_tables(db);
        if (status == LOCKER_STATUS_OK) {
            status = verify_schema_metadata(db);
        }
    }
    if (status == LOCKER_STATUS_OK) {
        status = run_quick_check(db);
    }

    locker_storage_schema_t *schema = NULL;
    locker_repo_t *repo = NULL;
    if (status == LOCKER_STATUS_OK) {
        schema = locker_storage_schema_create();
        if (schema == NULL) {
            /* 嵌入的 schema 子集解析失败说明构建产物损坏，不是数据库问题。 */
            status = LOCKER_STATUS_IO_ERROR;
        }
    }
    if (status == LOCKER_STATUS_OK) {
        repo = (locker_repo_t *)calloc(1u, sizeof(*repo));
        if (repo == NULL) {
            status = LOCKER_STATUS_NO_SPACE;
        }
    }
    if (status != LOCKER_STATUS_OK) {
        locker_storage_schema_destroy(schema);
        free(repo);
        (void)sqlite3_close_v2(db);
        return status;
    }
    repo->db = db;
    repo->schema = schema;
    repo->in_transaction = false;
    *out = repo;
    return LOCKER_STATUS_OK;
}

void locker_repo_close(locker_repo_t *repo)
{
    if (repo == NULL) {
        return;
    }
    locker_storage_rollback(repo);
    if (repo->db != NULL) {
        (void)sqlite3_close_v2(repo->db);
    }
    locker_storage_schema_destroy(repo->schema);
    free(repo);
}
