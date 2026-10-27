#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON.h"
#include "locker/common/uuid.h"
#include "locker/storage/event_key.h"
#include "locker/storage/u64.h"
#include "locker_contracts.h"
#include "sqlite3.h"

/* 仓储端口测试：只用契约里的六个 locker_repo_* 端口驱动，不调用任何内部函数。
 *
 * 覆盖本卡验收要求：
 *   - 同一事件重复导入不重复推进业务（不重复改投影、不推进游标）；
 *   - 失败事务不留下半个占位/outbox（整单回滚、revision 不动）；
 *   - 两个竞争申请只能一个成功（checks + expected_snapshot_revision）；
 *   - 数据库重开仍保留有效借用/未决操作/幂等结果；
 *   - 游标不能越过缺口、不能倒退、10 不能排在 2 前面。
 * 另外验证打开/初始化/损坏区分、WAL 生效与查询分页边界。
 * sqlite3 直连只用于构造「损坏/元数据被篡改」的测试夹具，不进入产品代码路径。
 */

static char g_dir[128];

/* 夹具用的最小文件写入（损坏库/空库）；正式代码路径不写文件。 */
static bool write_text_file(const char *path, const char *text);

#define TEST_DEVICE_ID "0123456789abcdef01234567"
#define UUID_TOOL "22222222-2222-4333-8444-555555555555"
#define UUID_USER "33333333-2222-4333-8444-555555555555"
#define UUID_OP "11111111-1111-4111-8111-111111111111"
#define UUID_SESSION "44444444-2222-4333-8444-555555555555"
#define UUID_HOST "55555555-2222-4333-8444-555555555555"
#define UUID_EPOCH "66666666-2222-4333-8444-555555555555"
#define UUID_LOAN "77777777-2222-4333-8444-555555555555"
#define UUID_OUTBOX "88888888-2222-4333-8444-555555555555"
#define UUID_KEY "99999999-2222-4333-8444-555555555555"

static const char *const k_snapshot =
    "{\"device_id\":\"" TEST_DEVICE_ID "\","
    "\"journal_epoch\":\"" UUID_EPOCH "\","
    "\"boot_id\":\"1\",\"physical_revision\":\"4\",\"door\":\"CLOSED\","
    "\"item\":\"PRESENT\",\"supply_mv\":4980,\"observed_age_ms\":120,"
    "\"time_quality\":\"HOST_SYNCED\",\"continuity_ok\":true,\"dropped_events\":\"0\","
    "\"durable_cursor\":{\"device_id\":\"" TEST_DEVICE_ID "\","
    "\"journal_epoch\":\"" UUID_EPOCH "\",\"boot_id\":\"1\",\"event_seq\":\"9\"}}";

/* ---------- 路径与打开辅助 ---------- */

static bool make_path(const char *name, char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s/%s", g_dir, name);
    return written > 0 && (size_t)written < out_size;
}

static locker_status_t open_repo_at(const char *path, bool initialize_empty, locker_repo_t **out)
{
    locker_repo_config_t config;
    memset(&config, 0, sizeof(config));
    int written = snprintf(config.path, sizeof(config.path), "%s", path);
    if (written <= 0 || (size_t)written >= sizeof(config.path)) {
        return LOCKER_STATUS_INVALID;
    }
    config.schema_version = 1u;
    config.initialize_empty = initialize_empty;
    return locker_repo_open(&config, out);
}

static locker_status_t open_new_repo(const char *name, locker_repo_t **out)
{
    char path[256];
    if (!make_path(name, path, sizeof(path))) {
        return LOCKER_STATUS_INVALID;
    }
    return open_repo_at(path, true, out);
}

static locker_status_t reopen_repo(const char *name, locker_repo_t **out)
{
    char path[256];
    if (!make_path(name, path, sizeof(path))) {
        return LOCKER_STATUS_INVALID;
    }
    return open_repo_at(path, false, out);
}

/* ---------- JSON 构造辅助 ---------- */

static int build_put(char *out, size_t out_size, const char *collection, const char *key,
                     const char *value_json)
{
    return snprintf(out, out_size,
                    "{\"op\":\"PUT\",\"document\":{\"collection\":\"%s\",\"key\":\"%s\",\"value\":%s}}",
                    collection, key, value_json);
}

static int build_delete(char *out, size_t out_size, const char *collection, const char *key)
{
    return snprintf(out, out_size,
                    "{\"op\":\"DELETE\",\"collection\":\"%s\",\"key\":\"%s\"}", collection, key);
}

static int build_change(char *out, size_t out_size, uint64_t expected, const char *checks_json,
                        const char *mutations_json)
{
    /* mutations_json 是逗号分隔的 mutation 列表（不带方括号），这里统一补成数组；
       checks_json 由调用方给出完整 JSON 数组。 */
    return snprintf(out, out_size,
                    "{\"schema_version\":1,\"expected_snapshot_revision\":\"%llu\","
                    "\"new_snapshot_revision\":\"%llu\",\"checks\":%s,\"mutations\":[%s]}",
                    (unsigned long long)expected, (unsigned long long)(expected + 1u), checks_json,
                    mutations_json);
}

static locker_status_t commit_json(locker_repo_t *repo, const char *json)
{
    locker_change_t change;
    memset(&change, 0, sizeof(change));
    size_t length = strlen(json);
    if (length >= sizeof(change.json)) {
        return LOCKER_STATUS_INVALID;
    }
    memcpy(change.json, json, length + 1u);
    change.length = length;
    return locker_repo_commit_change(repo, &change);
}

static locker_status_t commit_put(locker_repo_t *repo, uint64_t expected, const char *collection,
                                  const char *key, const char *value_json)
{
    char mutation[8192];
    char change[8192];
    if (build_put(mutation, sizeof(mutation), collection, key, value_json) <= 0 ||
        build_change(change, sizeof(change), expected, "[]", mutation) <= 0) {
        return LOCKER_STATUS_INVALID;
    }
    return commit_json(repo, change);
}

/* ---------- 记录构造辅助 ---------- */

static void build_user_record(char *out, size_t out_size, const char *username, uint64_t revision)
{
    snprintf(out, out_size,
             "{\"user\":{\"id\":\"" UUID_USER "\",\"username\":\"%s\",\"display_name\":\"Alice\","
             "\"role\":\"USER\",\"enabled\":true,\"user_revision\":\"%llu\"},"
             "\"password_hash\":\"$argon2id$v=19$m=65536,t=3,p=1$c2FsdA$aGFzaA\"}",
             username, (unsigned long long)revision);
}

static void build_session_record(char *out, size_t out_size, bool revoked)
{
    snprintf(out, out_size,
             "{\"id\":\"" UUID_SESSION "\",\"user_id\":\"" UUID_USER "\","
             "\"token_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
             "\"expires_at\":\"2026-10-24T00:00:00Z\",\"revoked\":%s}",
             revoked ? "true" : "false");
}

static void build_tool_record(char *out, size_t out_size, const char *name, uint64_t revision)
{
    snprintf(out, out_size,
             "{\"id\":\"" UUID_TOOL "\",\"name\":\"%s\",\"slot_id\":1,"
             "\"tool_revision\":\"%llu\",\"enabled\":true}",
             name, (unsigned long long)revision);
}

static void build_operation_record(char *out, size_t out_size, const char *state, uint64_t revision)
{
    snprintf(out, out_size,
             "{\"operation\":{\"id\":\"" UUID_OP "\",\"tool_id\":\"" UUID_TOOL "\","
             "\"kind\":\"BORROW\",\"actor_id\":\"" UUID_USER "\",\"state\":\"%s\","
             "\"operation_revision\":\"%llu\",\"command_id\":null,\"reason_code\":null,"
             "\"created_at\":\"2026-10-24T00:00:00Z\",\"updated_at\":\"2026-10-24T00:00:00Z\","
             "\"allowed_actions\":[\"PRESENT_AT_CABINET\"],"
             "\"evidence\":{\"history_complete\":true,\"door_open_seen\":false,"
             "\"item_transition_seen\":false,\"physical\":null,\"source_events\":[],"
             "\"manual_resolution\":false}},"
             "\"baseline\":%s,\"related_loan_id\":\"" UUID_LOAN "\","
             "\"host_instance_id\":\"" UUID_HOST "\","
             "\"proof_source\":\"CARD\",\"proof_consumed\":false}",
             state, (unsigned long long)revision, k_snapshot);
}

static void build_loan_record(char *out, size_t out_size)
{
    snprintf(out, out_size,
             "{\"id\":\"" UUID_LOAN "\",\"tool_id\":\"" UUID_TOOL "\","
             "\"borrower_id\":\"" UUID_USER "\",\"borrow_operation_id\":\"" UUID_OP "\","
             "\"return_operation_id\":null,\"opened_at\":\"2026-10-24T00:00:00Z\","
             "\"closed_at\":null}");
}

static void build_outbox_record(char *out, size_t out_size, const char *state)
{
    snprintf(out, out_size,
             "{\"id\":\"" UUID_OUTBOX "\",\"operation_id\":\"" UUID_OP "\","
             "\"session_id\":\"" UUID_SESSION "\",\"journal_epoch\":\"" UUID_EPOCH "\","
             "\"expected_boot_id\":\"1\",\"expected_physical_revision\":\"4\","
             "\"action_no\":\"1\",\"not_after_mcu_ms\":\"9000\",\"kind\":\"BORROW\","
             "\"slot_id\":1,\"state\":\"%s\",\"attempted_at\":null}",
             state);
}

static void build_idempotency_record(char *out, size_t out_size, int status)
{
    snprintf(out, out_size,
             "{\"actor_id\":\"" UUID_USER "\",\"method\":\"POST\",\"path\":\"/operations\","
             "\"key\":\"" UUID_KEY "\","
             "\"request_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
             "\"response_status\":%d,\"response_json\":\"{\\\"operation_id\\\":\\\""
             UUID_OP "\\\"}\",\"expires_at\":\"2026-10-24T00:00:00Z\"}",
             status);
}

static void build_slot_guard_record(char *out, size_t out_size)
{
    snprintf(out, out_size, "{\"slot_id\":1,\"operation_id\":\"" UUID_OP "\"}");
}

/* ---------- 查询辅助 ---------- */

static locker_status_t query_key(locker_repo_t *repo, const char *collection, const char *key,
                                 locker_repo_result_t *result)
{
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    snprintf(query.collection, sizeof(query.collection), "%s", collection);
    snprintf(query.key, sizeof(query.key), "%s", key);
    query.limit = 1u;
    return locker_repo_query(repo, &query, result);
}

static locker_status_t query_page(locker_repo_t *repo, const char *collection, const char *after_key,
                                  uint16_t limit, locker_repo_result_t *result)
{
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    snprintf(query.collection, sizeof(query.collection), "%s", collection);
    snprintf(query.after_key, sizeof(query.after_key), "%s", after_key);
    query.limit = limit;
    return locker_repo_query(repo, &query, result);
}

static locker_status_t recovery_page(locker_repo_t *repo, const char *collection,
                                     const char *after_key, uint16_t limit,
                                     locker_recovery_t *result)
{
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    snprintf(query.collection, sizeof(query.collection), "%s", collection);
    snprintf(query.after_key, sizeof(query.after_key), "%s", after_key);
    query.limit = limit;
    return locker_repo_load_recovery(repo, &query, result);
}

static cJSON *parse_result(const locker_repo_result_t *result)
{
    return cJSON_ParseWithLength(result->json, result->length);
}

static cJSON *parse_recovery(const locker_recovery_t *result)
{
    return cJSON_ParseWithLength(result->json, result->length);
}

static size_t documents_count(const cJSON *root)
{
    const cJSON *documents = cJSON_GetObjectItemCaseSensitive(root, "documents");
    if (!cJSON_IsArray(documents)) {
        return 0u;
    }
    return (size_t)cJSON_GetArraySize(documents);
}

static const cJSON *document_at(const cJSON *root, size_t index)
{
    const cJSON *documents = cJSON_GetObjectItemCaseSensitive(root, "documents");
    if (!cJSON_IsArray(documents)) {
        return NULL;
    }
    return cJSON_GetArrayItem(documents, (int)index);
}

/* ---------- 事件辅助 ---------- */

static void make_event(locker_device_event_t *event, uint64_t boot, uint64_t seq, uint16_t type)
{
    memset(event, 0, sizeof(*event));
    snprintf(event->key.device_id, sizeof(event->key.device_id), "%s", TEST_DEVICE_ID);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_uuid_parse(UUID_EPOCH, strlen(UUID_EPOCH), &event->key.journal_epoch));
    event->key.boot_id = boot;
    event->key.event_seq = seq;
    event->mono_ms = seq;
    event->physical_revision = seq;
    event->detail = 0u;
    event->event_type = type;
    event->reason = 0u;
    event->flags = 0u;
    event->operation_id = (locker_uuid_t){0};
    event->command_id = (locker_uuid_t){0};
    event->door = LOCKER_DOOR_STATE_CLOSED;
    event->item = LOCKER_ITEM_STATE_PRESENT;
    event->supply_mv = UINT32_MAX;
    event->card_uid_len = 0u;
    event->live_transport = true;
}

/* 按 uart-v1.md:368-372 的线协议语义填满一帧的边界字段：
   last_committed 是本页最后一条（MCU 已提交水位），next_after 有记录时就是本页最后一条，
   first_available 取本页第一条（MCU 声明的最早可用记录不晚于本页）。
   审计 B2 指出旧助手 memset 后从不填这三个字段，等于绕过了批次边界校验。 */
static locker_status_t import_events(locker_repo_t *repo, const locker_device_event_t *events,
                                     uint8_t count, bool gap_before_page,
                                     const locker_change_t *projection, locker_event_key_t *cursor)
{
    locker_event_batch_t batch;
    memset(&batch, 0, sizeof(batch));
    batch.count = count;
    batch.gap_before_page = gap_before_page;
    /* 边界测试会故意传 count=5（超过契约上限 4）：只复制到数组容量，count 原样保留
       让被测端口自己拒绝，避免测试助手先越界写。 */
    for (uint8_t i = 0u; i < count && i < 4u; i++) {
        batch.events[i] = events[i];
    }
    if (count >= 1u && count <= 4u) {
        batch.last_committed = events[count - 1u].key;
        batch.next_after = events[count - 1u].key;
        batch.first_available = events[0].key;
    }
    return locker_repo_import_events(repo, &batch, projection, cursor);
}

/* ---------- 1. 打开与初始化 ---------- */

static void test_open_and_initialize(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("init.db", &repo));
    TEST_CHECK(repo != NULL);
    if (repo == NULL) {
        return;
    }

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(0u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
        TEST_CHECK(cJSON_IsNull(next));
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
        TEST_CHECK(cJSON_IsNumber(version) && version->valueint == 1);
        cJSON_Delete(root);
    }
    locker_repo_close(repo);

    /* 重新打开已存在的库：不再要求 initialize_empty。 */
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, reopen_repo("init.db", &repo));
    TEST_CHECK(repo != NULL);
    locker_repo_close(repo);

    /* 库不存在且不允许初始化 → NOT_READY，不创建文件。 */
    char missing[256];
    TEST_CHECK(make_path("absent.db", missing, sizeof(missing)));
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, open_repo_at(missing, false, &repo));
    TEST_CHECK(repo == NULL);

    /* 参数校验：schema_version 只接受 1。 */
    locker_repo_config_t config;
    memset(&config, 0, sizeof(config));
    TEST_CHECK(make_path("badversion.db", config.path, sizeof(config.path)));
    config.schema_version = 2u;
    config.initialize_empty = true;
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_open(&config, &repo));
    TEST_CHECK(repo == NULL);

    /* 空路径必须拒绝。 */
    memset(&config, 0, sizeof(config));
    config.schema_version = 1u;
    config.initialize_empty = true;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_open(&config, &repo));

    /* close(NULL) 必须安全。 */
    locker_repo_close(NULL);
}

/* ---------- 2. 提交与查询 ---------- */

static void test_commit_put_and_query(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("commit.db", &repo));
    if (repo == NULL) {
        return;
    }

    char tool[1024];
    build_tool_record(tool, sizeof(tool), "heat gun", 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_TOOL, tool));

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        const cJSON *doc = document_at(root, 0u);
        TEST_CHECK(doc != NULL);
        if (doc != NULL) {
            const cJSON *collection = cJSON_GetObjectItemCaseSensitive(doc, "collection");
            const cJSON *key = cJSON_GetObjectItemCaseSensitive(doc, "key");
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(doc, "value");
            TEST_CHECK(cJSON_IsString(collection) && strcmp(collection->valuestring, "tools") == 0);
            TEST_CHECK(cJSON_IsString(key) && strcmp(key->valuestring, UUID_TOOL) == 0);
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(value, "name");
            TEST_CHECK(cJSON_IsString(name) && strcmp(name->valuestring, "heat gun") == 0);
            const cJSON *revision = cJSON_GetObjectItemCaseSensitive(value, "tool_revision");
            TEST_CHECK(cJSON_IsString(revision) && strcmp(revision->valuestring, "1") == 0);
        }
        cJSON_Delete(root);
    }

    /* 覆盖同一 key：值更新，revision 继续 +1。 */
    build_tool_record(tool, sizeof(tool), "heat gun v2", 2u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 1u, "tools", UUID_TOOL, tool));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(2u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        cJSON_Delete(root);
    }

    /* 未知 key：空页、next_after_key 为 null，不是错误。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_LOAN, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    /* 删除文档同样推进 revision。 */
    char mutation[512];
    char change[1024];
    TEST_CHECK(build_delete(mutation, sizeof(mutation), "tools", UUID_TOOL) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 2u, "[]", mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(3u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    /* 非法输入：NULL 指针、空 change、超长 length、坏 JSON、schema 不符。 */
    locker_change_t bad;
    memset(&bad, 0, sizeof(bad));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_commit_change(NULL, &bad));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_commit_change(repo, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_commit_change(repo, &bad));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_query(NULL, NULL, &result));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_load_recovery(NULL, NULL, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, commit_json(repo, "not json"));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      commit_json(repo, "{\"schema_version\":1,\"expected_snapshot_revision\":\"3\","
                                        "\"new_snapshot_revision\":\"4\",\"checks\":[],"
                                        "\"mutations\":[]}"));
    /* new_snapshot_revision 必须恰好是 expected + 1。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      commit_json(repo, "{\"schema_version\":1,\"expected_snapshot_revision\":\"3\","
                                        "\"new_snapshot_revision\":\"5\",\"checks\":[],"
                                        "\"mutations\":[{\"op\":\"DELETE\",\"collection\":\"tools\","
                                        "\"key\":\"k\"}]}"));
    /* 未知 collection。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      commit_json(repo, "{\"schema_version\":1,\"expected_snapshot_revision\":\"3\","
                                        "\"new_snapshot_revision\":\"4\",\"checks\":[],"
                                        "\"mutations\":[{\"op\":\"DELETE\",\"collection\":\"nope\","
                                        "\"key\":\"k\"}]}"));
    /* value 与 collection 不同型（tools 里放 UserRecord）。 */
    char user[1024];
    char user_mutation[2048];
    char user_change[4096];
    build_user_record(user, sizeof(user), "alice", 1u);
    TEST_CHECK(build_put(user_mutation, sizeof(user_mutation), "tools", UUID_USER, user) > 0);
    TEST_CHECK(build_change(user_change, sizeof(user_change), 3u, "[]", user_mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, commit_json(repo, user_change));

    /* 以上失败都不能改 revision。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(3u, result.snapshot_revision);

    locker_repo_close(repo);
}

/* ---------- 3. revision 冲突 ---------- */

static void test_revision_conflict(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("conflict.db", &repo));
    if (repo == NULL) {
        return;
    }

    char tool[1024];
    build_tool_record(tool, sizeof(tool), "multimeter", 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_TOOL, tool));

    /* 过期 expected revision → CONFLICT，且不落库。 */
    char tool2[1024];
    build_tool_record(tool2, sizeof(tool2), "oscilloscope", 2u);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, commit_put(repo, 0u, "tools", UUID_TOOL, tool2));

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        const cJSON *doc = document_at(root, 0u);
        const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
        const cJSON *name = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "name") : NULL;
        TEST_CHECK(cJSON_IsString(name) && strcmp(name->valuestring, "multimeter") == 0);
        cJSON_Delete(root);
    }

    /* checks 不满足 → CONFLICT。 */
    char mutation[512];
    char change[1024];
    TEST_CHECK(build_delete(mutation, sizeof(mutation), "tools", UUID_TOOL) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 1u,
                            "[{\"collection\":\"slot_guards\",\"key\":\"1\","
                            "\"condition\":\"EXISTS\"}]",
                            mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, commit_json(repo, change));
    /* ABSENT 条件成立时同一条 change 可以成功。 */
    TEST_CHECK(build_change(change, sizeof(change), 1u,
                            "[{\"collection\":\"slot_guards\",\"key\":\"1\","
                            "\"condition\":\"ABSENT\"}]",
                            mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(2u, result.snapshot_revision);

    locker_repo_close(repo);
}

/* ---------- 4. 事务原子性 ---------- */

static void test_failed_transaction_is_atomic(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("atomic.db", &repo));
    if (repo == NULL) {
        return;
    }

    /* 审计 N3：旧用例用非法 outbox state 制造失败，但 RepositoryChange 在 BEGIN
       之前就被端口校验拒绝，根本没进事务，不能证明「半写回滚」。这里改用 SQLite
       trigger：第一条 PUT 真实写入事务，第二条 INSERT 被 trigger ABORT，
       验证整单回滚（而不是只验证入参校验）。 */
    char path[256];
    TEST_CHECK(make_path("atomic.db", path, sizeof(path)));
    sqlite3 *raw = NULL;
    TEST_CHECK(sqlite3_open(path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw,
                                "CREATE TRIGGER poison_outbox BEFORE INSERT ON documents"
                                " WHEN NEW.collection = 'outbox'"
                                " BEGIN SELECT RAISE(ABORT, 'poisoned outbox write'); END",
                                NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }

    char guard[256];
    char outbox[1024];
    build_slot_guard_record(guard, sizeof(guard));
    build_outbox_record(outbox, sizeof(outbox), "PREPARED");

    char first[512];
    char second[2048];
    char mutations[4096];
    char change[8192];
    TEST_CHECK(build_put(first, sizeof(first), "slot_guards", "1", guard) > 0);
    TEST_CHECK(build_put(second, sizeof(second), "outbox", UUID_OUTBOX, outbox) > 0);
    TEST_CHECK(snprintf(mutations, sizeof(mutations), "%s,%s", first, second) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 0u, "[]", mutations) > 0);
    /* trigger 的 RAISE(ABORT) 走 SQLITE_CONSTRAINT，端口映射为 CONFLICT。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, commit_json(repo, change));

    /* 半个占位都不能留：slot_guards 仍为空，revision 仍为 0。 */
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "slot_guards", "1", &result));
    TEST_CHECK_U64(0u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "outbox", UUID_OUTBOX, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    /* 移除 poison trigger 后，同样两条必须一起成功（证明失败是 SQL 层而非入参）。 */
    raw = NULL;
    TEST_CHECK(sqlite3_open(path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw, "DROP TRIGGER poison_outbox", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }
    TEST_CHECK(build_put(second, sizeof(second), "outbox", UUID_OUTBOX, outbox) > 0);
    TEST_CHECK(snprintf(mutations, sizeof(mutations), "%s,%s", first, second) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 0u, "[]", mutations) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "slot_guards", "1", &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        cJSON_Delete(root);
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "outbox", UUID_OUTBOX, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);
}

/* ---------- 5. 竞争申请 ---------- */

static void test_competing_slot_guard(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("guard.db", &repo));
    if (repo == NULL) {
        return;
    }

    char guard[256];
    build_slot_guard_record(guard, sizeof(guard));
    char mutation[512];
    char change[2048];
    TEST_CHECK(build_put(mutation, sizeof(mutation), "slot_guards", "1", guard) > 0);
    /* 两个申请者都读到 revision 0，都用 checks=ABSENT 抢占同一柜格。 */
    TEST_CHECK(build_change(change, sizeof(change), 0u,
                            "[{\"collection\":\"slot_guards\",\"key\":\"1\","
                            "\"condition\":\"ABSENT\"}]",
                            mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, commit_json(repo, change));

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "slot_guards", "1", &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        cJSON_Delete(root);
    }

    /* 结束核验 operation_id 后按 DELETE 释放（x-rules 06）。 */
    char release[512];
    TEST_CHECK(build_delete(release, sizeof(release), "slot_guards", "1") > 0);
    TEST_CHECK(build_change(change, sizeof(change), 1u,
                            "[{\"collection\":\"slot_guards\",\"key\":\"1\","
                            "\"condition\":\"EXISTS\"}]",
                            release) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "slot_guards", "1", &result));
    TEST_CHECK_U64(2u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);
}

/* ---------- 5b. 真并发竞争申请（两个进程，各自独立连接） ---------- */

/* 子进程：独立打开同一个库文件，提交同一份「expected=0 + checks=ABSENT」申请，
   用退出码回传 locker_status_t。fork 之后才 open，避免跨进程复用 SQLite 连接。 */
static void race_child(const char *path, const char *change_json)
{
    locker_repo_t *repo = NULL;
    locker_status_t status = open_repo_at(path, false, &repo);
    if (status == LOCKER_STATUS_OK && repo != NULL) {
        status = commit_json(repo, change_json);
        locker_repo_close(repo);
    }
    _exit((int)status);
}

static void test_concurrent_slot_guard(void)
{
    /* 先建库与表结构，再由两个子进程竞争。 */
    locker_repo_t *setup = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("guard_race.db", &setup));
    if (setup == NULL) {
        return;
    }
    locker_repo_close(setup);

    char path[256];
    TEST_CHECK(make_path("guard_race.db", path, sizeof(path)));

    char guard[256];
    build_slot_guard_record(guard, sizeof(guard));
    char mutation[512];
    char change[2048];
    TEST_CHECK(build_put(mutation, sizeof(mutation), "slot_guards", "1", guard) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 0u,
                            "[{\"collection\":\"slot_guards\",\"key\":\"1\","
                            "\"condition\":\"ABSENT\"}]",
                            mutation) > 0);

    pid_t children[2] = {-1, -1};
    for (size_t i = 0u; i < 2u; i++) {
        children[i] = fork();
        TEST_CHECK(children[i] >= 0);
        if (children[i] == 0) {
            race_child(path, change);
            _exit((int)LOCKER_STATUS_IO_ERROR);
        }
    }

    int statuses[2] = {-1, -1};
    for (size_t i = 0u; i < 2u; i++) {
        if (children[i] > 0) {
            int raw = 0;
            TEST_CHECK(waitpid(children[i], &raw, 0) == children[i]);
            statuses[i] = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
        }
    }

    size_t ok_count = 0u;
    size_t conflict_count = 0u;
    for (size_t i = 0u; i < 2u; i++) {
        if (statuses[i] == (int)LOCKER_STATUS_OK) {
            ok_count++;
        } else if (statuses[i] == (int)LOCKER_STATUS_CONFLICT) {
            conflict_count++;
        }
    }
    TEST_CHECK(ok_count == 1u);
    TEST_CHECK(conflict_count == 1u);

    /* 柜格里只有一份占位，revision 只前进一次。 */
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, reopen_repo("guard_race.db", &repo));
    if (repo == NULL) {
        return;
    }
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "slot_guards", "1", &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        const cJSON *doc = document_at(root, 0u);
        const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
        const cJSON *operation = value != NULL
                                     ? cJSON_GetObjectItemCaseSensitive(value, "operation_id")
                                     : NULL;
        TEST_CHECK(cJSON_IsString(operation) && strcmp(operation->valuestring, UUID_OP) == 0);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);
}

/* ---------- 6. 重开保留 ---------- */

static void test_persistence_across_reopen(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("persist.db", &repo));
    if (repo == NULL) {
        return;
    }

    char operation[4096];
    char loan[1024];
    char outbox[1024];
    char idem[1024];
    char tool[1024];
    char session[1024];
    build_operation_record(operation, sizeof(operation), "ACCESS_PENDING", 1u);
    build_loan_record(loan, sizeof(loan));
    build_outbox_record(outbox, sizeof(outbox), "SEND_ATTEMPTED");
    build_idempotency_record(idem, sizeof(idem), 202);
    build_tool_record(tool, sizeof(tool), "soldering iron", 1u);
    build_session_record(session, sizeof(session), false);

    char m1[8192];
    char m2[2048];
    char m3[2048];
    char m4[2048];
    char m5[2048];
    char m6[2048];
    char mutations[16384];
    char change[32768];
    TEST_CHECK(build_put(m1, sizeof(m1), "tools", UUID_TOOL, tool) > 0);
    TEST_CHECK(build_put(m2, sizeof(m2), "operations", UUID_OP, operation) > 0);
    TEST_CHECK(build_put(m3, sizeof(m3), "loans", UUID_LOAN, loan) > 0);
    TEST_CHECK(build_put(m4, sizeof(m4), "outbox", UUID_OUTBOX, outbox) > 0);
    TEST_CHECK(build_put(m5, sizeof(m5), "idempotency", UUID_KEY, idem) > 0);
    TEST_CHECK(build_put(m6, sizeof(m6), "sessions", UUID_SESSION, session) > 0);
    TEST_CHECK(snprintf(mutations, sizeof(mutations), "%s,%s,%s,%s,%s,%s", m1, m2, m3, m4, m5, m6) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 0u, "[]", mutations) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));
    locker_repo_close(repo);

    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, reopen_repo("persist.db", &repo));
    TEST_CHECK(repo != NULL);
    if (repo == NULL) {
        return;
    }

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "loans", UUID_LOAN, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        const cJSON *doc = document_at(root, 0u);
        const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
        const cJSON *borrower = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "borrower_id") : NULL;
        TEST_CHECK(cJSON_IsString(borrower) && strcmp(borrower->valuestring, UUID_USER) == 0);
        const cJSON *closed = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "closed_at") : NULL;
        TEST_CHECK(cJSON_IsNull(closed));
        cJSON_Delete(root);
    }

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "operations", UUID_OP, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        const cJSON *doc = document_at(root, 0u);
        const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
        const cJSON *op = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "operation") : NULL;
        const cJSON *state = op != NULL ? cJSON_GetObjectItemCaseSensitive(op, "state") : NULL;
        TEST_CHECK(cJSON_IsString(state) && strcmp(state->valuestring, "ACCESS_PENDING") == 0);
        cJSON_Delete(root);
    }

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "outbox", UUID_OUTBOX, &result));
    root = parse_result(&result);
    if (root != NULL) {
        const cJSON *doc = document_at(root, 0u);
        const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
        const cJSON *state = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "state") : NULL;
        TEST_CHECK(cJSON_IsString(state) && strcmp(state->valuestring, "SEND_ATTEMPTED") == 0);
        cJSON_Delete(root);
    }

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "idempotency", UUID_KEY, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        const cJSON *doc = document_at(root, 0u);
        const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
        const cJSON *status = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "response_status") : NULL;
        TEST_CHECK(cJSON_IsNumber(status) && status->valueint == 202);
        cJSON_Delete(root);
    }

    /* 重开后 revision 不倒退，下一次提交按持久化值校验。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, commit_put(repo, 0u, "tools", UUID_TOOL, tool));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 1u, "tools", UUID_TOOL, tool));

    locker_repo_close(repo);
}

/* ---------- 7. 事件重复导入 ---------- */

static void test_import_events_dedup(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("events.db", &repo));
    if (repo == NULL) {
        return;
    }

    locker_device_event_t events[2];
    make_event(&events[0], 1u, 1u, 1u);
    make_event(&events[1], 1u, 2u, 2u);
    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, events, 2u, false, NULL, &cursor));
    TEST_CHECK_U64(1u, cursor.boot_id);
    TEST_CHECK_U64(2u, cursor.event_seq);

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    TEST_CHECK_U64(0u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        cJSON_Delete(root);
    }

    /* 重复导入同一批：OK、游标不前进、投影不变、事件不重复、revision 不动。
       这里故意带一个会推进 revision 的 projection_change，验证重复时它不生效。 */
    char tool[1024];
    char mutation[1024];
    char projection[4096];
    build_tool_record(tool, sizeof(tool), "wire cutter", 1u);
    TEST_CHECK(build_put(mutation, sizeof(mutation), "tools", UUID_TOOL, tool) > 0);
    TEST_CHECK(build_change(projection, sizeof(projection), 0u, "[]", mutation) > 0);
    locker_change_t projection_change;
    memset(&projection_change, 0, sizeof(projection_change));
    snprintf(projection_change.json, sizeof(projection_change.json), "%s", projection);
    projection_change.length = strlen(projection_change.json);

    locker_event_key_t duplicate_cursor;
    memset(&duplicate_cursor, 0, sizeof(duplicate_cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      import_events(repo, events, 2u, false, &projection_change, &duplicate_cursor));
    TEST_CHECK_U64(2u, duplicate_cursor.event_seq);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    TEST_CHECK_U64(0u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        cJSON_Delete(root);
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    /* 新事件带合法投影：事件与投影同一事务提交，revision +1。 */
    locker_device_event_t more[1];
    make_event(&more[0], 1u, 3u, 4u);
    locker_event_key_t next_cursor;
    memset(&next_cursor, 0, sizeof(next_cursor));
    TEST_CHECK(build_change(projection, sizeof(projection), 0u, "[]", mutation) > 0);
    snprintf(projection_change.json, sizeof(projection_change.json), "%s", projection);
    projection_change.length = strlen(projection_change.json);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      import_events(repo, more, 1u, false, &projection_change, &next_cursor));
    TEST_CHECK_U64(3u, next_cursor.event_seq);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_TOOL, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);

    /* 投影非法 → 事件与投影都回滚，游标不前进。 */
    locker_device_event_t bad_page[1];
    make_event(&bad_page[0], 1u, 4u, 5u);
    char bad_projection[4096];
    /* 故意用已过期的 expected revision（当前是 1）制造投影事务失败。 */
    TEST_CHECK(build_change(bad_projection, sizeof(bad_projection), 0u, "[]", mutation) > 0);
    snprintf(projection_change.json, sizeof(projection_change.json), "%s", bad_projection);
    projection_change.length = strlen(projection_change.json);
    locker_event_key_t bad_cursor;
    memset(&bad_cursor, 0, sizeof(bad_cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      import_events(repo, bad_page, 1u, false, &projection_change, &bad_cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 3u);
        cJSON_Delete(root);
    }

    /* 参数边界：count 0 和 count 5 都非法。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, import_events(repo, events, 0u, false, NULL, &next_cursor));
    locker_device_event_t five[5];
    for (uint8_t i = 0u; i < 5u; i++) {
        make_event(&five[i], 1u, (uint64_t)(10u + i), 1u);
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, import_events(repo, five, 5u, false, NULL, &next_cursor));

    locker_repo_close(repo);
}

/* ---------- 8. 缺口与倒退 ---------- */

static void test_import_events_gap_and_regression(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("gap.db", &repo));
    if (repo == NULL) {
        return;
    }

    locker_device_event_t page[2];
    make_event(&page[0], 1u, 1u, 1u);
    make_event(&page[1], 1u, 2u, 2u);
    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));

    /* 跳号：seq 5（缺口 3、4）必须 CONFLICT，且不落库。 */
    locker_device_event_t gap[1];
    make_event(&gap[0], 1u, 5u, 3u);
    locker_event_key_t gap_cursor;
    memset(&gap_cursor, 0, sizeof(gap_cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, import_events(repo, gap, 1u, false, NULL, &gap_cursor));
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        cJSON_Delete(root);
    }

    /* 页内乱序（先 4 后 3）必须 INVALID。 */
    locker_device_event_t unordered[2];
    make_event(&unordered[0], 1u, 4u, 1u);
    make_event(&unordered[1], 1u, 3u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      import_events(repo, unordered, 2u, false, NULL, &gap_cursor));

    /* 补齐 3、4 后可以继续。 */
    locker_device_event_t fill[2];
    make_event(&fill[0], 1u, 3u, 1u);
    make_event(&fill[1], 1u, 4u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, fill, 2u, false, NULL, &cursor));
    TEST_CHECK_U64(4u, cursor.event_seq);

    /* 倒退：已经到 boot 2 之后，再导入 boot 1 的新事件必须 CONFLICT。 */
    locker_device_event_t boot2[1];
    make_event(&boot2[0], 2u, 1u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, boot2, 1u, false, NULL, &cursor));
    TEST_CHECK_U64(2u, cursor.boot_id);
    locker_device_event_t old_boot[1];
    make_event(&old_boot[0], 1u, 7u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, import_events(repo, old_boot, 1u, false, NULL, &cursor));

    /* 越过 MCU 已提交水位（last_committed）必须 INVALID。 */
    locker_device_event_t beyond[1];
    make_event(&beyond[0], 2u, 2u, 1u);
    locker_event_batch_t batch;
    memset(&batch, 0, sizeof(batch));
    batch.count = 1u;
    batch.events[0] = beyond[0];
    batch.last_committed = beyond[0].key;
    batch.last_committed.event_seq = 1u;
    locker_event_key_t beyond_cursor;
    memset(&beyond_cursor, 0, sizeof(beyond_cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &beyond_cursor));

    /* 首次接入时带 gap_before_page：不连续但事件本身仍要落库（continuity_ok=false）。 */
    locker_repo_t *second = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("gap2.db", &second));
    if (second != NULL) {
        locker_device_event_t start[1];
        make_event(&start[0], 1u, 5u, 1u);
        locker_event_key_t second_cursor;
        memset(&second_cursor, 0, sizeof(second_cursor));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          import_events(second, start, 1u, true, NULL, &second_cursor));
        locker_recovery_t recovery;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                          recovery_page(second, "import_cursors", "", 10u, &recovery));
        cJSON *recovery_root = parse_recovery(&recovery);
        if (recovery_root != NULL) {
            TEST_CHECK(documents_count(recovery_root) == 1u);
            const cJSON *doc = document_at(recovery_root, 0u);
            const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
            const cJSON *continuity = value != NULL
                                          ? cJSON_GetObjectItemCaseSensitive(value, "continuity_ok")
                                          : NULL;
            TEST_CHECK(cJSON_IsBool(continuity) && !cJSON_IsTrue(continuity));
            cJSON_Delete(recovery_root);
        }
        locker_repo_close(second);
    }

    locker_repo_close(repo);
}

/* ---------- 8b. 事件批次边界与连续性（审计 B1/B2 回归） ---------- */

/* 构造一页事件并填好合法的线协议边界字段（first_available/last_committed/next_after），
   供边界用例逐项破坏：破坏后必须被端口拒绝且不落库。 */
static void make_batch(locker_event_batch_t *batch, locker_device_event_t *events, uint8_t count)
{
    memset(batch, 0, sizeof(*batch));
    batch->count = count;
    for (uint8_t i = 0u; i < count && i < 4u; i++) {
        batch->events[i] = events[i];
    }
    if (count >= 1u && count <= 4u) {
        batch->first_available = events[0].key;
        batch->last_committed = events[count - 1u].key;
        batch->next_after = events[count - 1u].key;
    }
}

/* 从 import_cursors 里读回 through（boot/seq）与 continuity_ok，用于断言水位和连续性标记。 */
static bool read_import_cursor(locker_repo_t *repo, uint64_t *out_boot, uint64_t *out_seq,
                               bool *out_continuity)
{
    locker_recovery_t recovery;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, recovery_page(repo, "import_cursors", "", 10u, &recovery));
    cJSON *root = parse_recovery(&recovery);
    if (root == NULL) {
        return false;
    }
    bool ok = false;
    const cJSON *doc = document_at(root, 0u);
    const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
    const cJSON *through = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "through") : NULL;
    const cJSON *boot = through != NULL ? cJSON_GetObjectItemCaseSensitive(through, "boot_id") : NULL;
    const cJSON *seq = through != NULL ? cJSON_GetObjectItemCaseSensitive(through, "event_seq") : NULL;
    const cJSON *continuity =
        value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "continuity_ok") : NULL;
    uint64_t boot_value = 0u;
    uint64_t seq_value = 0u;
    if (cJSON_IsString(boot) && cJSON_IsString(seq) && cJSON_IsBool(continuity) &&
        locker_storage_u64_parse(boot->valuestring, strlen(boot->valuestring), &boot_value) &&
        locker_storage_u64_parse(seq->valuestring, strlen(seq->valuestring), &seq_value)) {
        if (out_boot != NULL) {
            *out_boot = boot_value;
        }
        if (out_seq != NULL) {
            *out_seq = seq_value;
        }
        if (out_continuity != NULL) {
            *out_continuity = cJSON_IsTrue(continuity) != 0;
        }
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

static void test_import_page_boundaries(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("boundary.db", &repo));
    if (repo == NULL) {
        return;
    }

    /* 页内缺号：1、3 升序但跳过 2，MCU 声称本页连续却漏事件，必须 INVALID 且一条都不落库
       （审计 B1-A：旧实现只比相邻事件大小，1 < 3 就放行）。 */
    locker_device_event_t gap_page[2];
    make_event(&gap_page[0], 1u, 1u, 1u);
    make_event(&gap_page[1], 1u, 3u, 1u);
    locker_event_batch_t gap_batch;
    make_batch(&gap_batch, gap_page, 2u);
    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &gap_batch, NULL, &cursor));
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    /* 基线：1、2 连续的一页必须接受。 */
    locker_device_event_t page[2];
    make_event(&page[0], 1u, 1u, 1u);
    make_event(&page[1], 1u, 2u, 1u);
    locker_event_batch_t good;
    make_batch(&good, page, 2u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_repo_import_events(repo, &good, NULL, &cursor));
    TEST_CHECK_U64(2u, cursor.event_seq);

    /* 下面每个用例只破坏一个边界字段，事件本身（3、4 连续）始终合法。 */
    locker_device_event_t next[2];
    make_event(&next[0], 1u, 3u, 1u);
    make_event(&next[1], 1u, 4u, 1u);
    locker_event_batch_t batch;
    locker_event_key_t rejected;
    memset(&rejected, 0, sizeof(rejected));

    /* 1) last_committed 为零：非空页不可能「一条都没提交」，旧实现直接跳过整个上界检查。 */
    make_batch(&batch, next, 2u);
    memset(&batch.last_committed, 0, sizeof(batch.last_committed));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 2) last_committed 属于另一台设备：旧比较函数先比 device_id，可用别的设备号绕过数值水位。 */
    make_batch(&batch, next, 2u);
    TEST_CHECK(snprintf(batch.last_committed.device_id, sizeof(batch.last_committed.device_id),
                        "fedcba9876543210fedcba98") > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 3) last_committed 属于另一个 journal_epoch：换了日志就不是同一段历史。 */
    make_batch(&batch, next, 2u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_uuid_parse("77777777-2222-4333-8444-555555555555", 36u,
                                        &batch.last_committed.journal_epoch));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 4) 本页越过 MCU 已提交水位：声称只到 seq 3 却发了 4。 */
    make_batch(&batch, next, 2u);
    batch.last_committed.event_seq = 3u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 5) next_after 与末条不一致：有记录时线协议要求 next_after 就是末条。 */
    make_batch(&batch, next, 2u);
    batch.next_after.event_seq = 3u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 6) first_available 晚于本页首条：本页明明记录了更早的可用记录。 */
    make_batch(&batch, next, 2u);
    batch.first_available = next[1].key;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 7) last_committed 半零（boot=2, seq=0）：既不是 (0,0) 哨兵也不是事件位置。
          uart-v1.md:38「禁止仅其中一个为 0」，旧实现会拿它当数值边界参与比较
          （审计 round-2 B2）。 */
    make_batch(&batch, next, 2u);
    batch.last_committed.boot_id = 2u;
    batch.last_committed.event_seq = 0u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 8) first_available 半零（boot=0, seq=1）：同样是结构非法的边界游标。 */
    make_batch(&batch, next, 2u);
    batch.first_available.boot_id = 0u;
    batch.first_available.event_seq = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 9) next_after 半零（boot=1, seq=0）：next_after 必须是本页最后一条。 */
    make_batch(&batch, next, 2u);
    batch.next_after.event_seq = 0u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 10) 非空页把 first_available 的 boot/seq 清零但保留 device/epoch：这是真正危险的
           半零变体——同日志校验照样通过、数值比较把 (0,0) 当成「最早」，于是「最早可用
           记录」被悄悄抹掉。必须由哨兵结构校验拒绝（审计 round-2 B2、uart-v1.md:370）。 */
    make_batch(&batch, next, 2u);
    batch.first_available.boot_id = 0u;
    batch.first_available.event_seq = 0u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 10b) 整体清零（连 device/epoch 一起）：同日志校验就会拒，但也要固定住这个行为。 */
    make_batch(&batch, next, 2u);
    memset(&batch.first_available, 0, sizeof(batch.first_available));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_repo_import_events(repo, &batch, NULL, &rejected));

    /* 十次拒绝都不能留下任何痕迹：事件仍 2 条、水位仍停在 seq 2、连续性仍为 true。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        cJSON_Delete(root);
    }
    uint64_t boot = 0u;
    uint64_t seq = 0u;
    bool continuity = false;
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(1u, boot);
    TEST_CHECK_U64(2u, seq);
    TEST_CHECK(continuity);

    /* 边界全部修正后同一页必须被接受：证明拒绝原因是边界字段，而不是事件本身。 */
    make_batch(&batch, next, 2u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_repo_import_events(repo, &batch, NULL, &cursor));
    TEST_CHECK_U64(4u, cursor.event_seq);

    locker_repo_close(repo);
}

/* 边界字段可以比本页更宽（审计 round-3 N-3 / round-4 N-2）：first_available 是「这条
   日志里最早还可用的记录位置」，last_committed 是「MCU 已经持久提交的日志尾」——不是
   RK 本端落库水位（RK 自己的水位是 committed_cursor/through），两者都可能早于/晚于当前页；
   只有 next_after 在非空页必须等于本页末条（uart-v1.md:368-370）。旧文档把三者一律写成
   「本页首/末条」，这条成功对照就是用来钉住真实语义的。 */
static void test_import_boundary_envelope(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("envelope.db", &repo));
    if (repo == NULL) {
        return;
    }

    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    locker_device_event_t page[2];
    make_event(&page[0], 1u, 1u, 1u);
    make_event(&page[1], 1u, 2u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));
    TEST_CHECK_U64(2u, cursor.event_seq);

    /* 本页只有 3、4：最早可用记录仍是 seq 1，已提交尾已到 seq 9，has_more=1。 */
    make_event(&page[0], 1u, 3u, 1u);
    make_event(&page[1], 1u, 4u, 1u);
    locker_event_batch_t batch;
    make_batch(&batch, page, 2u);
    batch.first_available.boot_id = 1u;
    batch.first_available.event_seq = 1u;
    batch.last_committed.boot_id = 1u;
    batch.last_committed.event_seq = 9u;
    batch.has_more = true;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_repo_import_events(repo, &batch, NULL, &cursor));
    /* 水位只推进到本页末条，不跳到 last_committed：ACK 的是「我落库到哪」。 */
    TEST_CHECK_U64(4u, cursor.event_seq);

    uint64_t boot = 0u;
    uint64_t seq = 0u;
    bool continuity = false;
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(1u, boot);
    TEST_CHECK_U64(4u, seq);
    TEST_CHECK(continuity);

    locker_repo_close(repo);
}

static void test_import_continuity_latching(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("latch.db", &repo));
    if (repo == NULL) {
        return;
    }

    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    locker_device_event_t page[2];
    make_event(&page[0], 1u, 1u, 1u);
    make_event(&page[1], 1u, 2u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));

    uint64_t boot = 0u;
    uint64_t seq = 0u;
    bool continuity = false;
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK(continuity);

    /* 已有游标时收到 gap_before_page=true：本页事件连续也必须把缺口锁存成 continuity_ok=false
       （审计 B1-B：旧实现直接继承 stored_continuity，把缺口洗白了）。 */
    make_event(&page[0], 1u, 3u, 1u);
    make_event(&page[1], 1u, 4u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, true, NULL, &cursor));
    TEST_CHECK_U64(4u, cursor.event_seq);
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(4u, seq);
    TEST_CHECK(!continuity);

    /* 后续正常页不能把已经锁存的缺口洗回 true。 */
    make_event(&page[0], 1u, 5u, 1u);
    make_event(&page[1], 1u, 6u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(6u, seq);
    TEST_CHECK(!continuity);

    /* 换 boot：boot 号可以跳（那次启动没有落任何事件时 RK 根本看不到它，不是事件缺口），
       所以 boot 2 之后直接来 boot 4 必须接受。 */
    locker_device_event_t next_boot[1];
    make_event(&next_boot[0], 2u, 1u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, next_boot, 1u, false, NULL, &cursor));
    TEST_CHECK_U64(2u, cursor.boot_id);
    make_event(&next_boot[0], 4u, 1u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, next_boot, 1u, false, NULL, &cursor));
    TEST_CHECK_U64(4u, cursor.boot_id);
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(4u, boot);
    TEST_CHECK_U64(1u, seq);

    /* boot 倒退（boot 3 排在 boot 4 之后）不是合法推进，必须 CONFLICT 且水位不动。 */
    make_event(&next_boot[0], 3u, 1u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      import_events(repo, next_boot, 1u, false, NULL, &cursor));
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(4u, boot);
    TEST_CHECK_U64(1u, seq);

    locker_repo_close(repo);
}

/* boot 号跳号不是事件缺口：continuity_ok 为 true 时，boot 1 直接跳到 boot 5
   必须仍算连续（RK 看不到没有落事件的启动）。审计 round-2 N1 指出旧回归里
   跳 boot 的起点已经是 continuity=false，测不出这一点。 */
static void test_boot_jump_keeps_continuity(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("bootjump.db", &repo));
    if (repo == NULL) {
        return;
    }

    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    locker_device_event_t page[2];
    make_event(&page[0], 1u, 1u, 1u);
    make_event(&page[1], 1u, 2u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));

    uint64_t boot = 0u;
    uint64_t seq = 0u;
    bool continuity = false;
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK(continuity);

    locker_device_event_t jump[1];
    make_event(&jump[0], 5u, 1u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, jump, 1u, false, NULL, &cursor));
    TEST_CHECK_U64(5u, cursor.boot_id);
    TEST_CHECK_U64(1u, cursor.event_seq);
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(5u, boot);
    TEST_CHECK(continuity);

    locker_repo_close(repo);
}

/* 全重复页 + gap_before_page=true：一条事件都不用写，但「本页之前有缺口」是
   新事实，必须锁存成 continuity_ok=false（审计 round-2 B1：早退发生在 gap 锁存之前，
   缺口会被后续正常页洗白）。 */
static void test_duplicate_page_latches_gap(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("dup_gap.db", &repo));
    if (repo == NULL) {
        return;
    }

    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    locker_device_event_t page[2];
    make_event(&page[0], 1u, 1u, 1u);
    make_event(&page[1], 1u, 2u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));

    uint64_t boot = 0u;
    uint64_t seq = 0u;
    bool continuity = false;
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(2u, seq);
    TEST_CHECK(continuity);

    /* 同一页原样重发，但这一帧带 gap_before_page=true：事件全重复。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, true, NULL, &cursor));
    TEST_CHECK_U64(2u, cursor.event_seq);
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(2u, seq);
    TEST_CHECK(!continuity);

    /* 后续正常页不能把已经锁存的缺口洗回 true。 */
    make_event(&page[0], 1u, 3u, 1u);
    make_event(&page[1], 1u, 4u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, false, NULL, &cursor));
    TEST_CHECK_U64(4u, cursor.event_seq);
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(4u, seq);
    TEST_CHECK(!continuity);

    locker_repo_close(repo);
}

/* 同 boot 历史回填：先用 gap 页把水位推到 seq 6（3..4 从未导入），再回填 3、4。
   水位不得倒退（审计 B1-C），必须 CONFLICT 且水位仍停在 seq 6。 */
static void test_import_backfill_rejected(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("backfill.db", &repo));
    if (repo == NULL) {
        return;
    }
    locker_event_key_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    locker_device_event_t page[2];
    make_event(&page[0], 1u, 5u, 1u);
    make_event(&page[1], 1u, 6u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, 2u, true, NULL, &cursor));

    uint64_t boot = 0u;
    uint64_t seq = 0u;
    bool continuity = false;
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(1u, boot);
    TEST_CHECK_U64(6u, seq);
    TEST_CHECK(!continuity);

    locker_device_event_t backfill[2];
    make_event(&backfill[0], 1u, 3u, 1u);
    make_event(&backfill[1], 1u, 4u, 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      import_events(repo, backfill, 2u, false, NULL, &cursor));
    TEST_CHECK(read_import_cursor(repo, &boot, &seq, &continuity));
    TEST_CHECK_U64(1u, boot);
    TEST_CHECK_U64(6u, seq);

    /* 回填同样不能写进 device_events：被拒的页必须一条都不落库。 */
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 100u, &result));
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);
}

/* ---------- 9. 游标与分页顺序 ---------- */

static void test_cursor_order_and_pagination(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("order.db", &repo));
    if (repo == NULL) {
        return;
    }

    /* 逐页补齐 1..10：每页 2 条，验证 10 不会排到 2 前面。 */
    uint64_t next_seq = 1u;
    while (next_seq <= 10u) {
        locker_device_event_t page[2];
        uint8_t count = 0u;
        for (uint8_t i = 0u; i < 2u && next_seq <= 10u; i++) {
            make_event(&page[count], 1u, next_seq, 1u);
            count++;
            next_seq++;
        }
        locker_event_key_t cursor;
        memset(&cursor, 0, sizeof(cursor));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, import_events(repo, page, count, false, NULL, &cursor));
        TEST_CHECK_U64(next_seq - 1u, cursor.event_seq);
    }

    /* 分页读：limit=3，按事件键字节序即数值序。 */
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "device_events", "", 3u, &result));
    cJSON *root = parse_result(&result);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 3u);
        const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
        TEST_CHECK(cJSON_IsString(next));
        char after[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
        memset(after, 0, sizeof(after));
        if (cJSON_IsString(next) && strlen(next->valuestring) < sizeof(after)) {
            snprintf(after, sizeof(after), "%s", next->valuestring);
        }
        cJSON_Delete(root);

        /* 用 next_after_key 继续翻页，收齐 10 条并检查 seq 单调（首屏 3 条已读）。 */
        uint64_t previous = 3u;
        size_t total = 3u;
        bool saw_ten = false;
        for (;;) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                              query_page(repo, "device_events", after, 3u, &result));
            root = parse_result(&result);
            TEST_CHECK(root != NULL);
            if (root == NULL) {
                break;
            }
            size_t count = documents_count(root);
            for (size_t i = 0u; i < count; i++) {
                const cJSON *doc = document_at(root, i);
                const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
                const cJSON *key = value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "key") : NULL;
                const cJSON *seq = key != NULL ? cJSON_GetObjectItemCaseSensitive(key, "event_seq") : NULL;
                uint64_t parsed = 0u;
                TEST_CHECK(cJSON_IsString(seq) &&
                           locker_storage_u64_parse(seq->valuestring, strlen(seq->valuestring), &parsed));
                TEST_CHECK(parsed > previous);
                previous = parsed;
                if (parsed == 10u) {
                    saw_ten = true;
                }
                total++;
            }
            const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
            bool has_next = cJSON_IsString(next);
            if (has_next && strlen(next->valuestring) < sizeof(after)) {
                snprintf(after, sizeof(after), "%s", next->valuestring);
            }
            cJSON_Delete(root);
            if (!has_next) {
                break;
            }
        }
        TEST_CHECK(total == 10u);
        TEST_CHECK(saw_ten);
        TEST_CHECK_U64(10u, previous);
    }

    /* 分页参数边界：limit 0 与 101 都非法；key 与 after_key 互斥。 */
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    snprintf(query.collection, sizeof(query.collection), "device_events");
    query.limit = 0u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_query(repo, &query, &result));
    query.limit = 101u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_query(repo, &query, &result));
    query.limit = 1u;
    snprintf(query.key, sizeof(query.key), "%s", UUID_TOOL);
    /* key 与 after_key 互斥：两者同时非空必须拒绝。 */
    snprintf(query.after_key, sizeof(query.after_key), "x");
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_query(repo, &query, &result));
    query.key[0] = '\0';
    snprintf(query.after_key, sizeof(query.after_key), "x");
    query.limit = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_repo_query(repo, &query, &result));
    /* 未知 collection 与错误 schema_version。 */
    snprintf(query.collection, sizeof(query.collection), "nope");
    query.after_key[0] = '\0';
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_query(repo, &query, &result));
    snprintf(query.collection, sizeof(query.collection), "device_events");
    query.schema_version = 2u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_query(repo, &query, &result));

    locker_repo_close(repo);
}

/* ---------- 10. 恢复分页 ---------- */

static void test_recovery_pages(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("recovery.db", &repo));
    if (repo == NULL) {
        return;
    }

    char tool[1024];
    for (uint64_t i = 0u; i < 5u; i++) {
        char key[64];
        snprintf(key, sizeof(key), "tool-%llu", (unsigned long long)i);
        build_tool_record(tool, sizeof(tool), "tool", i);
        /* tools 的 key 是 uuid，这里只用合法 uuid 前缀区分 5 条记录。 */
        char uuid[40];
        snprintf(uuid, sizeof(uuid), "00000000-0000-4000-8000-00000000000%llu",
                 (unsigned long long)i);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, i, "tools", uuid, tool));
    }

    locker_recovery_t recovery;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, recovery_page(repo, "tools", "", 2u, &recovery));
    TEST_CHECK_U64(5u, recovery.snapshot_revision);
    cJSON *root = parse_recovery(&recovery);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        const cJSON *collection = cJSON_GetObjectItemCaseSensitive(root, "collection");
        TEST_CHECK(cJSON_IsString(collection) && strcmp(collection->valuestring, "tools") == 0);
        const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
        TEST_CHECK(cJSON_IsString(next));
        char after[256];
        memset(after, 0, sizeof(after));
        if (cJSON_IsString(next) && strlen(next->valuestring) < sizeof(after)) {
            snprintf(after, sizeof(after), "%s", next->valuestring);
        }
        cJSON_Delete(root);

        size_t total = 2u;
        for (;;) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, recovery_page(repo, "tools", after, 2u, &recovery));
            root = parse_recovery(&recovery);
            TEST_CHECK(root != NULL);
            if (root == NULL) {
                break;
            }
            total += documents_count(root);
            const cJSON *next_key = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
            bool has_next = cJSON_IsString(next_key);
            if (has_next && strlen(next_key->valuestring) < sizeof(after)) {
                snprintf(after, sizeof(after), "%s", next_key->valuestring);
            }
            cJSON_Delete(root);
            if (!has_next) {
                break;
            }
        }
        TEST_CHECK(total == 5u);
    }

    /* 恢复页不接受单键查询（x-rules 10）。 */
    locker_repo_query_t query;
    memset(&query, 0, sizeof(query));
    query.schema_version = 1u;
    snprintf(query.collection, sizeof(query.collection), "tools");
    snprintf(query.key, sizeof(query.key), UUID_TOOL);
    query.limit = 1u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_load_recovery(repo, &query, &recovery));

    locker_repo_close(repo);
}

/* ---------- 10b. 分页按最终序列化预算（审计 B4 回归） ---------- */

/* 一条合法 AuditRecord，reason 用 256 个中文字符（3 字节/码点）把单条文档撑到约 1 KB，
   100 条必然超过 65535 字节页上限，用来逼出「整页 NO_SPACE」的分页缺陷。 */
static void build_audit_record(char *out, size_t out_size, uint64_t index, const char *reason)
{
    snprintf(out, out_size,
             "{\"id\":\"%08llx-0000-4000-8000-%012llx\",\"actor_id\":null,"
             "\"operation_id\":null,\"type\":\"PAGE_BUDGET\",\"reason\":\"%s\","
             "\"occurred_at\":null,\"time_quality\":\"HOST_SYNCED\",\"source_event\":null}",
             (unsigned long long)index, (unsigned long long)index, reason);
}

static void fill_audits(locker_repo_t *repo, size_t count, const char *reason)
{
    char record[2048];
    char uuid[40];
    for (uint64_t i = 0u; i < (uint64_t)count; i++) {
        /* 测试索引不超过 100，用 32 位十六进制格式化即可；写成 %08llx 会让编译器
           按「最多 16 位」估算宽度并报 -Wformat-truncation。 */
        snprintf(uuid, sizeof(uuid), "%08x-0000-4000-8000-%012x", (unsigned)i, (unsigned)i);
        build_audit_record(record, sizeof(record), i, reason);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, i, "audits", uuid, record));
    }
}

static void test_pagination_budget(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("budget.db", &repo));
    if (repo == NULL) {
        return;
    }

    char reason[800];
    size_t pos = 0u;
    for (size_t i = 0u; i < 256u; i++) {
        int written = snprintf(reason + pos, sizeof(reason) - pos, "%s", "验");
        if (written <= 0 || (size_t)written >= sizeof(reason) - pos) {
            break;
        }
        pos += (size_t)written;
    }
    TEST_CHECK(pos == 768u);
    fill_audits(repo, 100u, reason);

    /* 查询分页：一页装不下 100 条时必须缩成「非空页 + 续页键」，而不是整页 NO_SPACE
       （审计 B4：旧实现只累计 key+value 字节，忽略 JSON 包装与转义，选中 67 条后
       序列化超 65535，直接失败且不给续页）。 */
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "audits", "", 100u, &result));
    TEST_CHECK(result.length <= 65535u);
    cJSON *root = parse_result(&result);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        size_t first_page = documents_count(root);
        TEST_CHECK(first_page > 0u);
        TEST_CHECK(first_page < 100u);
        const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
        TEST_CHECK(cJSON_IsString(next));
        char after[192];
        memset(after, 0, sizeof(after));
        if (cJSON_IsString(next) && strlen(next->valuestring) < sizeof(after)) {
            snprintf(after, sizeof(after), "%s", next->valuestring);
        }
        cJSON_Delete(root);

        size_t total = first_page;
        size_t pages = 1u;
        bool complete = true;
        while (strlen(after) > 0u && pages < 200u) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "audits", after, 100u, &result));
            TEST_CHECK(result.length <= 65535u);
            root = parse_result(&result);
            if (root == NULL) {
                complete = false;
                break;
            }
            total += documents_count(root);
            const cJSON *next_key = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
            memset(after, 0, sizeof(after));
            if (cJSON_IsString(next_key) && strlen(next_key->valuestring) < sizeof(after)) {
                snprintf(after, sizeof(after), "%s", next_key->valuestring);
            }
            cJSON_Delete(root);
            pages++;
        }
        TEST_CHECK(complete);
        TEST_CHECK(total == 100u);
        TEST_CHECK(pages > 1u);
    }

    /* 恢复分页走同一套预算：也要缩页 + 续页键，且带 collection 字段的页头不能把预算算错。 */
    locker_recovery_t recovery;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, recovery_page(repo, "audits", "", 100u, &recovery));
    TEST_CHECK(recovery.length <= 65535u);
    root = parse_recovery(&recovery);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        size_t first_page = documents_count(root);
        TEST_CHECK(first_page > 0u);
        TEST_CHECK(first_page < 100u);
        const cJSON *collection = cJSON_GetObjectItemCaseSensitive(root, "collection");
        TEST_CHECK(cJSON_IsString(collection) && strcmp(collection->valuestring, "audits") == 0);
        const cJSON *next = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
        TEST_CHECK(cJSON_IsString(next));
        char after[192];
        memset(after, 0, sizeof(after));
        if (cJSON_IsString(next) && strlen(next->valuestring) < sizeof(after)) {
            snprintf(after, sizeof(after), "%s", next->valuestring);
        }
        cJSON_Delete(root);

        size_t total = first_page;
        size_t pages = 1u;
        while (strlen(after) > 0u && pages < 200u) {
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, recovery_page(repo, "audits", after, 100u, &recovery));
            TEST_CHECK(recovery.length <= 65535u);
            root = parse_recovery(&recovery);
            if (root == NULL) {
                break;
            }
            total += documents_count(root);
            const cJSON *next_key = cJSON_GetObjectItemCaseSensitive(root, "next_after_key");
            memset(after, 0, sizeof(after));
            if (cJSON_IsString(next_key) && strlen(next_key->valuestring) < sizeof(after)) {
                snprintf(after, sizeof(after), "%s", next_key->valuestring);
            }
            cJSON_Delete(root);
            pages++;
        }
        TEST_CHECK(total == 100u);
    }

    locker_repo_close(repo);
}

/* ---------- 10c. 嵌入 NUL 拒绝（审计 B5 回归） ---------- */

/* cJSON 用 NUL 结尾的 C 字符串保存值，\u0000 解码后是 U+0000：一旦放过，后续 strlen
   会静默截断（"0\u0000junk" 被当成 U64 的 0），且重新序列化时后缀丢失。必须在解析入口
   fail closed，覆盖属性名、key、U64 文本与原始 NUL 字节，同时不能误伤合法的转义反斜杠。 */
/* 每个 NUL 负例都必须在自己的干净库上跑：共用一个库时，前一个用例若被放行会推进 revision，
   后续用例的失败就只是连带效应，不能证明各类别独立覆盖（审计 round-4 N-2）。 */
static void nul_case_expect_invalid(const char *name, const locker_change_t *change)
{
    char file[160];
    snprintf(file, sizeof(file), "nul_%s.db", name);
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo(file, &repo));
    if (repo == NULL) {
        return;
    }
    char tool[1024];
    build_tool_record(tool, sizeof(tool), "power supply", 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_TOOL, tool));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_repo_commit_change(repo, change));

    /* 拒绝必须无副作用：revision 仍是 1、tools 记录仍是原样。 */
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "tools", "", 100u, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        const cJSON *doc = document_at(root, 0u);
        const cJSON *key = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "key") : NULL;
        TEST_CHECK(cJSON_IsString(key) && strcmp(key->valuestring, UUID_TOOL) == 0);
        cJSON_Delete(root);
    }
    locker_repo_close(repo);
}

static void nul_case_expect_invalid_json(const char *name, const char *change_json)
{
    locker_change_t change;
    memset(&change, 0, sizeof(change));
    size_t length = strlen(change_json);
    TEST_CHECK(length < sizeof(change.json));
    if (length >= sizeof(change.json)) {
        return;
    }
    memcpy(change.json, change_json, length + 1u);
    change.length = length;
    nul_case_expect_invalid(name, &change);
}

/* 原始 0x00 字节版本：head 与 tail 之间插一个真 NUL，模拟 cJSON 之前就已存在的裸字节。 */
static void nul_case_expect_invalid_raw(const char *name, const char *head, const char *tail)
{
    locker_change_t change;
    memset(&change, 0, sizeof(change));
    size_t head_len = strlen(head);
    size_t tail_len = strlen(tail);
    TEST_CHECK(head_len + 1u + tail_len <= sizeof(change.json));
    if (head_len + 1u + tail_len > sizeof(change.json)) {
        return;
    }
    memcpy(change.json, head, head_len);
    change.json[head_len] = '\0';
    memcpy(change.json + head_len + 1u, tail, tail_len);
    change.length = head_len + 1u + tail_len;
    nul_case_expect_invalid(name, &change);
}

static void test_embedded_nul_rejected(void)
{
    /* 每个负例都必须是「若无 NUL 守卫就会被接受」的完整合法变更，且各自使用干净库：
       否则断言虽然通过，测到的却是别的校验或前一个用例的连带失败
       （审计 round-2 N1、round-3 N-2、round-4 N-2）。 */
    char valid_tool[1024];
    build_tool_record(valid_tool, sizeof(valid_tool), "power supply", 2u);
    char change[4096];

    /* 1) expected_snapshot_revision 里的 \u0000：解码后 revision 变成 "1" 加垃圾后缀，
          截断后恰好等于当前 revision；mutations 是合法 PUT，唯一拒绝理由只能是 NUL。 */
    snprintf(change, sizeof(change),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\\u0000junk\","
             "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":["
             "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\",\"key\":\"" UUID_TOOL "\","
             "\"value\":%s}}]}",
             valid_tool);
    nul_case_expect_invalid_json("expected_rev", change);

    /* 2) 文档 key 里的 \u0000：键被截断后可能覆盖另一条记录。
          值用合法 ToolRecord，保证不是被值校验挡下的。 */
    snprintf(change, sizeof(change),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
             "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":["
             "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\","
             "\"key\":\"" UUID_TOOL "\\u0000x\",\"value\":%s}}]}",
             valid_tool);
    nul_case_expect_invalid_json("key_mid", change);

    /* 2b) collection 里的 \u0000：截断后恰好是合法集合名 "tools"，所以除 NUL 守卫外
           没有任何别的理由拒绝它（审计 round-3 N-2：key 开头 NUL 截断成空 key，会先被
           minLength 拒，不能算独立证据；集合名这一条才是独立的）。 */
    snprintf(change, sizeof(change),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
             "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":["
             "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\\u0000junk\","
             "\"key\":\"" UUID_KEY "\",\"value\":%s}}]}",
             valid_tool);
    nul_case_expect_invalid_json("collection", change);

    /* 2c) NUL 在 key 开头：截断后是空 key，会被 minLength 1 先拒，因此这一条只能作为
           补充用例，不能单独证明 NUL 守卫生效（诚实登记，不当作独立证据）。 */
    snprintf(change, sizeof(change),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
             "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":["
             "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\","
             "\"key\":\"\\u0000junk\",\"value\":%s}}]}",
             valid_tool);
    nul_case_expect_invalid_json("key_head", change);

    /* 3) 记录字段（U64 文本）里的 \u0000：这正是审计复现的绕过路径。 */
    const char *nul_value =
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\",\"key\":\"" UUID_TOOL "\","
        "\"value\":{\"id\":\"" UUID_TOOL "\",\"name\":\"power supply\",\"slot_id\":1,"
        "\"tool_revision\":\"1\\u0000junk\",\"enabled\":true}}}";
    snprintf(change, sizeof(change),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
             "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":[%s]}",
             nul_value);
    nul_case_expect_invalid_json("u64_text", change);

    /* 4) 属性名里的 \u0000：截断后是 "name"——一个合法且必需的属性名，字段值也合法，
          因此除 NUL 守卫外没有任何别的理由拒绝（审计 round-3 N-2：旧的 "na\u0000me"
          截断成 "na"，本就会被 additionalProperties 拒，属空洞负例）。 */
    const char *nul_property =
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\",\"key\":\"" UUID_TOOL "\","
        "\"value\":{\"id\":\"" UUID_TOOL "\",\"name\\u0000junk\":\"power supply\","
        "\"slot_id\":1,\"tool_revision\":\"2\",\"enabled\":true}}}";
    snprintf(change, sizeof(change),
             "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
             "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":[%s]}",
             nul_property);
    nul_case_expect_invalid_json("property_escaped", change);

    /* 4b) 原始 NUL 字节在属性名里：与 4 同一类，但走的是裸字节分支。
           截断后同样是合法的 "name"，唯一拒绝理由只能是 NUL（审计 round-3 N-2）。 */
    nul_case_expect_invalid_raw(
        "property_raw",
        "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
        "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":["
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\","
        "\"key\":\"" UUID_TOOL "\",\"value\":{\"id\":\"" UUID_TOOL "\","
        "\"name",
        "junk\":\"power supply\",\"slot_id\":1,\"tool_revision\":\"2\","
        "\"enabled\":true}}}]}");

    /* 5a) 原始 NUL 字节在 expected_snapshot_revision 里：cJSON 把 0x00 原样拷进
           valuestring，strlen 截断后 "1\0junk" 变成 "1"，恰好等于当前 revision，
           若无守卫变更会被放行（这正是本轮修正的假阳性）。 */
    nul_case_expect_invalid_raw(
        "rev_raw", "{\"schema_version\":1,\"expected_snapshot_revision\":\"1",
        "junk\",\"new_snapshot_revision\":\"2\",\"checks\":[],"
        "\"mutations\":[{\"op\":\"PUT\",\"document\":{\"collection\":\"audits\","
        "\"key\":\"00000000-0000-4000-8000-0000000000bb\",\"value\":{"
        "\"id\":\"00000000-0000-4000-8000-0000000000bb\",\"actor_id\":null,"
        "\"operation_id\":null,\"type\":\"RAW_NUL\",\"reason\":\"r\","
        "\"occurred_at\":null,\"time_quality\":\"UNSYNCED\",\"source_event\":null}}}]}");

    /* 5b) 原始 NUL 字节在记录字段里：reason "a\0b" 截断成 "a" 仍满足 minLength 1。 */
    nul_case_expect_invalid_raw(
        "reason_raw",
        "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
        "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":[{"
        "\"op\":\"PUT\",\"document\":{\"collection\":\"audits\","
        "\"key\":\"00000000-0000-4000-8000-0000000000cc\",\"value\":{"
        "\"id\":\"00000000-0000-4000-8000-0000000000cc\",\"actor_id\":null,"
        "\"operation_id\":null,\"type\":\"RAW_NUL\",\"reason\":\"a",
        "b\",\"occurred_at\":null,\"time_quality\":\"UNSYNCED\","
        "\"source_event\":null}}}]}");

    /* 4a) 正对照：把 4) 的 "name\u0000junk" 换成 "name"，同一份 JSON 在干净库里必须
           被接受。这条对照证明 4) 的拒绝确实来自 NUL，而不是别的校验。 */
    {
        locker_repo_t *positive = NULL;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("nul_pos.db", &positive));
        if (positive != NULL) {
            const char *clean_property =
                "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\","
                "\"key\":\"" UUID_TOOL "\",\"value\":{\"id\":\"" UUID_TOOL "\","
                "\"name\":\"power supply\",\"slot_id\":1,\"tool_revision\":\"1\","
                "\"enabled\":true}}}";
            char positive_change[4096];
            snprintf(positive_change, sizeof(positive_change),
                     "{\"schema_version\":1,\"expected_snapshot_revision\":\"0\","
                     "\"new_snapshot_revision\":\"1\",\"checks\":[],\"mutations\":[%s]}",
                     clean_property);
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(positive, positive_change));
            locker_repo_close(positive);
        }
    }

    /* 合法的转义反斜杠不能被误伤：\"\\\\u0000\" 解码后是 6 个字符的反斜杠文本。 */
    {
        locker_repo_t *repo = NULL;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("nul_backslash.db", &repo));
        if (repo != NULL) {
            char tool[1024];
            build_tool_record(tool, sizeof(tool), "power supply", 1u);
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_TOOL, tool));

            const char *escaped_backslash =
                "{\"op\":\"PUT\",\"document\":{\"collection\":\"audits\",\"key\":\""
                "00000000-0000-4000-8000-0000000000aa\",\"value\":{\"id\":\""
                "00000000-0000-4000-8000-0000000000aa\",\"actor_id\":null,\"operation_id\":null,"
                "\"type\":\"ESCAPED_BACKSLASH\",\"reason\":\"\\\\u0000\",\"occurred_at\":null,"
                "\"time_quality\":\"UNSYNCED\",\"source_event\":null}}}";
            char escaped_change[4096];
            snprintf(escaped_change, sizeof(escaped_change),
                     "{\"schema_version\":1,\"expected_snapshot_revision\":\"1\","
                     "\"new_snapshot_revision\":\"2\",\"checks\":[],\"mutations\":[%s]}",
                     escaped_backslash);
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, escaped_change));

            locker_repo_result_t result;
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "audits", "", 10u, &result));
            cJSON *root = parse_result(&result);
            TEST_CHECK(root != NULL);
            if (root != NULL) {
                TEST_CHECK(documents_count(root) == 1u);
                const cJSON *doc = document_at(root, 0u);
                const cJSON *value = doc != NULL ? cJSON_GetObjectItemCaseSensitive(doc, "value") : NULL;
                const cJSON *reason_value =
                    value != NULL ? cJSON_GetObjectItemCaseSensitive(value, "reason") : NULL;
                TEST_CHECK(cJSON_IsString(reason_value) &&
                           strcmp(reason_value->valuestring, "\\u0000") == 0);
                cJSON_Delete(root);
            }
            locker_repo_close(repo);
        }
    }
}

/* documents 这个名字被 VIEW 占用（审计 round-2 B3）：DDL 的 CREATE TABLE IF NOT EXISTS
   会静默跳过同名视图，旧实现因此第一次 open 假成功并写入元数据、第二次才 CONFLICT。
   现在必须在首次 open 就拒绝，且不留下任何写入痕迹。 */
static void test_documents_view_rejected(void)
{
    char view_path[256];
    TEST_CHECK(make_path("docview.db", view_path, sizeof(view_path)));
    sqlite3 *raw = NULL;
    TEST_CHECK(sqlite3_open(view_path, &raw) == SQLITE_OK);
    if (raw == NULL) {
        return;
    }
    TEST_CHECK(sqlite3_exec(raw,
                            "CREATE VIEW documents AS SELECT 'loans' AS collection,"
                            " 'x' AS key, '{}' AS value_json WHERE 0;",
                            NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(raw);

    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(view_path, true, &repo));
    TEST_CHECK(repo == NULL);
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(view_path, false, &repo));
    TEST_CHECK(repo == NULL);

    /* 目标必须原样保留：只有一个视图对象，没有新增 meta/migrations 表。 */
    raw = NULL;
    TEST_CHECK(sqlite3_open(view_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(raw,
                               "SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'",
                               -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                TEST_CHECK(sqlite3_column_int(stmt, 0) == 1);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(raw);
    }
}

/* 直接用 SQLite 打开一个已存在的库执行 DDL/DML，只用于构造夹具。 */
static bool fixture_exec(const char *path, const char *sql)
{
    sqlite3 *raw = NULL;
    if (sqlite3_open(path, &raw) != SQLITE_OK) {
        if (raw != NULL) {
            sqlite3_close(raw);
        }
        return false;
    }
    bool ok = sqlite3_exec(raw, sql, NULL, NULL, NULL) == SQLITE_OK;
    sqlite3_close(raw);
    return ok;
}

/* 数 sqlite_master 里的用户对象（排除 sqlite_ 内部前缀，用 GLOB 保证下划线是字面量）。 */
static bool fixture_object_count(const char *path, int *out_count)
{
    sqlite3 *raw = NULL;
    if (sqlite3_open(path, &raw) != SQLITE_OK) {
        if (raw != NULL) {
            sqlite3_close(raw);
        }
        return false;
    }
    bool ok = false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(raw,
                           "SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'",
                           -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *out_count = sqlite3_column_int(stmt, 0);
            ok = true;
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(raw);
    return ok;
}

/* 审计 round-3 B-1：db_user_object_count 的 SQL 里 LIKE 的 '_' 是单字符通配符，
   用户建的 sqlitex_audit（合法名字）会被当成内部对象跳过，有痕迹库被当成空库补建。
   现在改用 GLOB，这类对象必须被计入「已有痕迹」。 */
static void test_sqlite_prefixed_user_object_rejected(void)
{
    char table_path[256];
    TEST_CHECK(make_path("sqlitex_table.db", table_path, sizeof(table_path)));
    TEST_CHECK(fixture_exec(table_path, "CREATE TABLE sqlitex_audit(kept INTEGER);"));

    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(table_path, true, &repo));
    TEST_CHECK(repo == NULL);
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(table_path, false, &repo));
    TEST_CHECK(repo == NULL);

    /* 原对象必须原样保留：没有被补建 meta/migrations/documents。 */
    int count = -1;
    TEST_CHECK(fixture_object_count(table_path, &count));
    TEST_CHECK(count == 1);

    /* 同名 VIEW 也是「已有对象」，同样不能当空库。 */
    char view_path[256];
    TEST_CHECK(make_path("sqlitex_view.db", view_path, sizeof(view_path)));
    TEST_CHECK(fixture_exec(view_path,
                            "CREATE VIEW sqlitex_view AS SELECT 1 AS kept WHERE 0;"));
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(view_path, true, &repo));
    TEST_CHECK(repo == NULL);
    count = -1;
    TEST_CHECK(fixture_object_count(view_path, &count));
    TEST_CHECK(count == 1);
}

/* ---------- 10. 打开库时的 schema 结构核验 ---------- */

/* 审计 round-3 B-2 / round-4 B-1：只查「三张表名存在」不够，列结构、主键位置、键唯一索引
   的 BINARY 比较规则、以及列自身的默认排序规则都要核；否则 open 返回 OK、第一次 query/PUT
   才炸，或者大小写不同的两个 key 被同时匹配、被一起 DELETE。

   夹具分两步做，保证变异日志能按**调用点行号**归因到具体夹具：
   先 setup（建真库再改坏），再对同一个库做 open(false)/open(true) 两次探测。 */

static bool broken_schema_setup(const char *file, const char *sql)
{
    char path[256];
    if (!make_path(file, path, sizeof(path))) {
        return false;
    }
    locker_repo_t *repo = NULL;
    if (open_repo_at(path, true, &repo) != LOCKER_STATUS_OK) {
        return false;
    }
    locker_repo_close(repo);
    return fixture_exec(path, sql);
}

static locker_status_t broken_schema_probe(const char *file, bool initialize_empty)
{
    char path[256];
    if (!make_path(file, path, sizeof(path))) {
        return LOCKER_STATUS_INVALID;
    }
    locker_repo_t *repo = NULL;
    locker_status_t status = open_repo_at(path, initialize_empty, &repo);
    TEST_CHECK(repo == NULL);
    if (repo != NULL) {
        /* 意外成功时也要关闭，否则泄漏的句柄会让后续夹具的失败原因变得不可诊断。 */
        locker_repo_close(repo);
    }
    return status;
}

/* ① 缺 value_json 列：旧实现 open 返回 OK，query 报 no such column。 */
#define SQL_MISSING_COLUMN                                                        \
    "DROP TABLE documents;"                                                       \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL);"

/* ② 三列齐全但没有主键/唯一约束：PUT 的 ON CONFLICT(collection,key) 无法 prepare。 */
#define SQL_NO_PRIMARY_KEY                                                        \
    "DROP TABLE documents;"                                                       \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"         \
    " value_json TEXT NOT NULL);"

/* ③ 有主键但 collection 用 NOCASE：键唯一性与排序语义都变了。 */
#define SQL_NOCASE_KEY                                                            \
    "DROP TABLE documents;"                                                       \
    "CREATE TABLE documents(collection TEXT NOT NULL COLLATE NOCASE,"             \
    " key TEXT NOT NULL, value_json TEXT NOT NULL, PRIMARY KEY(collection, key));"

/* ④ 多一列：主键/唯一索引完全合法（键索引检查会放行），只有列结构检查能拒。 */
#define SQL_EXTRA_COLUMN                                                          \
    "DROP TABLE documents;"                                                       \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"         \
    " value_json TEXT NOT NULL, extra TEXT NOT NULL, PRIMARY KEY(collection, key));"

/* ⑤ 列默认排序规则是 NOCASE、主键显式 COLLATE BINARY（审计 round-4 B-1）：
   索引 xinfo 报 BINARY，键索引检查会放行；但 WHERE/ORDER BY 用的是列排序规则，
   大小写不同的两个 key 会同时被匹配、被一起 DELETE。只有列级 collation 检查能拒。 */
#define SQL_COLUMN_COLLATION                                                      \
    "DROP TABLE documents;"                                                       \
    "CREATE TABLE documents(collection TEXT NOT NULL COLLATE NOCASE,"             \
    " key TEXT NOT NULL COLLATE NOCASE, value_json TEXT NOT NULL,"                \
    " PRIMARY KEY(collection COLLATE BINARY, key COLLATE BINARY));"

/* ⑥ 列名/列数/主键位置都对，只有类型错（value_json 声明成 INTEGER）：
   列结构检查里的类型比较是唯一能拒它的地方（键索引与列排序规则都合法）。 */
#define SQL_WRONG_TYPE                                                            \
    "DROP TABLE documents;"                                                       \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"         \
    " value_json INTEGER NOT NULL, PRIMARY KEY(collection, key));"

/* ⑧ 列默认 BINARY、但主键显式 COLLATE NOCASE，并额外建一个 BINARY 唯一索引
   （审计 round-4 B-1）：键索引检查若「找到任一匹配索引就放行」会接受它，而实际
   键唯一性按 NOCASE 判，字节不同的两个 key 被同一个约束拒绝。 */
#define SQL_REVERSE_COLLATION                                                      \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key COLLATE NOCASE));"     \
    "CREATE UNIQUE INDEX binary_key ON documents(collection COLLATE BINARY,"       \
    " key COLLATE BINARY);"

/* ⑨ 多一个挂在别的列上的唯一索引：主键完全合法，但它会改变可接受键集合。 */
#define SQL_EXTRA_UNIQUE_INDEX                                                     \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key));"                    \
    "CREATE UNIQUE INDEX unique_value ON documents(value_json);"

/* ⑩ 主键完全合法、列排序规则全是 BINARY，只多一个 partial unique index
   （审计 round-5 B-1）：它不构成全局唯一约束，但确实会拒绝一部分本来合法的写入
   （两条 collection 同为 audits、UUID 不同的记录会因为 collection 列相同而冲突）。
   partial 索引必须在 index_list 的 partial 列上判，xinfo 看不出来。 */
#define SQL_PARTIAL_UNIQUE_INDEX                                                   \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key));"                    \
    "CREATE UNIQUE INDEX partial_collection ON documents(collection)"              \
    " WHERE collection = 'audits';"

/* ⑪ 官方三表之外多一个预存触发器（审计 round-6 B-1）：只核列/键/metadata 的 open 会放行，
   之后命中 WHEN 的 PUT 被 RAISE(IGNORE) 静默跳过（changes()==0 且无 SQL 错误），端口仍报
   成功并写 revision。对象清单必须恰好等于官方 DDL 产出的三张表，多一个对象就 fail-closed。 */
#define SQL_TRIGGER_IGNORE                                                         \
    "CREATE TRIGGER skip_second BEFORE INSERT ON documents"                        \
    " WHEN NEW.key = '" UUID_KEY "'"                                               \
    " BEGIN SELECT RAISE(IGNORE); END;"

/* ⑫ 把 value_json 本身声明成 VIRTUAL 生成列（审计 round-6 B-2，round-7 收紧）：
   pragma_table_info 会直接漏掉这一列（只报 collection/key 两列），而 pragma_table_xinfo
   报出的列名/类型/NOT NULL/pk 与官方三列完全一致，唯一差别是 hidden=2。这样「隐藏列」
   检查才是唯一能拒它的防线（若只比列名/列数，table_xinfo 的三列会被误判为合法）。 */
#define SQL_GENERATED_COLUMN                                                       \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL GENERATED ALWAYS AS (key) VIRTUAL,"                 \
    " PRIMARY KEY(collection, key)) WITHOUT ROWID;"

/* ⑫b documents 末尾追加 VIRTUAL 生成列（round-6 B-2 的原始反例）：table_info 仍报 3 列，
   列名/类型/NOT NULL/pk 与官方一致，只有 table_xinfo 的 hidden 能看见它。它和 ⑫ 分别覆盖
   「换回 table_info」与「不查 hidden」两种回退，缺一个就会有一条防线失去独立证据。 */
#define SQL_GENERATED_APPENDED                                                     \
    "ALTER TABLE documents ADD COLUMN extra TEXT GENERATED ALWAYS AS"              \
    " (CASE WHEN collection = 'audits' THEN NULL ELSE key END) VIRTUAL NOT NULL;"

/* ⑬–⑮ 审计 round-7：真实校验顺序是「列检查 → 键索引检查 → 对象清单检查 → DDL 文本比对」
   （repository.c 的 verify_schema_tables）。round-6 引入的对象清单检查在键索引检查**之后**
   仍会把 ⑧⑨⑩ 那类「额外 CREATE INDEX」夹具拒掉，所以只关掉键索引检查时用例照旧通过
   （变异 m18/m20b/m22/m26 单点全绿）。这里改用**表内联约束**：内联 UNIQUE / 内联主键生成
   的自动索引名以 sqlite_autoindex_ 开头，不产生新的用户对象，对象清单检查放行，只有键索引
   检查能拒——把该分支重新变成可独立观测的防线（round-7 又加了 DDL 文本兜底，因此还需要
   对应的联合变异点位才能独立观测）。 */
#define SQL_INLINE_EXTRA_UNIQUE                                                    \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key),"                     \
    " UNIQUE(value_json));"

/* ⑭ 内联主键把 key 声明成 NOCASE：列自身声明仍是默认 BINARY（列级 collation 检查放行），
   只有 pragma_index_xinfo 报出的索引列排序规则是 NOCASE。 */
#define SQL_INLINE_PK_NOCASE                                                       \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key COLLATE NOCASE));"

/* ⑮ 内联主键把 key 声明成 DESC：table_xinfo 与列级 collation 都看不出来，
   只有索引 xinfo 的 desc 标志能拒。 */
#define SQL_INLINE_PK_DESC                                                         \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key DESC));"

/* ⑯ 表内 CHECK 约束（审计 round-7 B-1）：列结构、主键索引、对象清单、metadata、
   quick_check 全都与官方一致——CHECK 不产生额外 sqlite_master 对象，也不改变任何
   pragma 视图。只有把 CREATE TABLE 文本与唯一 schema-v1.sql 逐字节比对才能识别。
   命中 CHECK 的合法写入会被 SQLITE_CONSTRAINT 拒绝，open 却会假接纳。 */
#define SQL_CHECK_CONSTRAINT                                                       \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key),"                     \
    " CHECK(key <> '" UUID_KEY "')) WITHOUT ROWID;"

/* ⑰ 表内 FOREIGN KEY（审计 round-7 B-1 的同源变体）：同样不新增对象、不改列/索引视图；
   但 open 会 PRAGMA foreign_keys=ON，之后第一条合法写入就报 foreign key mismatch。
   与 ⑯ 同根因，不另算缺陷，但要有独立回归证明这条防线不只覆盖 CHECK。 */
#define SQL_FOREIGN_KEY                                                            \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents(collection TEXT NOT NULL, key TEXT NOT NULL,"          \
    " value_json TEXT NOT NULL, PRIMARY KEY(collection, key),"                     \
    " FOREIGN KEY(collection) REFERENCES meta(value)) WITHOUT ROWID;"

/* ⑱ 与官方 documents 文本**等长但内容不同**（关键字全部小写，语义等价）：
   列/索引/对象/metadata/quick_check 全同，长度也相同，只有逐字节内容比较能区分。
   本卡接纳策略是「只接受唯一 schema-v1.sql 的受控官方定义」，不做 SQL 语义等价判定，
   因此这种写法按策略拒绝；它专门用于让「只比长度不比内容」的回退（变异 m34）可观测。 */
#define SQL_DDL_SAME_LEN_DIFFERENT_TEXT                                            \
    "DROP TABLE documents;"                                                        \
    "CREATE TABLE documents (\n"                                                   \
    "    collection text not null,\n"                                              \
    "    key        text not null,\n"                                              \
    "    value_json text not null,\n"                                              \
    "    primary key (collection, key)\n"                                          \
    ") without rowid;"

static void test_schema_structure_verified(void)
{
    /* 对照：真库重开必须 OK。 */
    {
        locker_repo_t *repo = NULL;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("struct_ok.db", &repo));
        if (repo != NULL) {
            locker_repo_close(repo);
        }
        repo = NULL;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, reopen_repo("struct_ok.db", &repo));
        if (repo != NULL) {
            locker_repo_close(repo);
        }
    }

    TEST_CHECK(broken_schema_setup("struct_missing.db", SQL_MISSING_COLUMN));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_missing.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_missing.db", true));

    TEST_CHECK(broken_schema_setup("struct_nopk.db", SQL_NO_PRIMARY_KEY));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_nopk.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_nopk.db", true));

    TEST_CHECK(broken_schema_setup("struct_nocase.db", SQL_NOCASE_KEY));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_nocase.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_nocase.db", true));

    TEST_CHECK(broken_schema_setup("struct_extra.db", SQL_EXTRA_COLUMN));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_extra.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_extra.db", true));

    TEST_CHECK(broken_schema_setup("struct_colcoll.db", SQL_COLUMN_COLLATION));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_colcoll.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_colcoll.db", true));

    TEST_CHECK(broken_schema_setup("struct_wrongtype.db", SQL_WRONG_TYPE));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_wrongtype.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_wrongtype.db", true));

    TEST_CHECK(broken_schema_setup("struct_revcoll.db", SQL_REVERSE_COLLATION));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_revcoll.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_revcoll.db", true));

    TEST_CHECK(broken_schema_setup("struct_extrauniq.db", SQL_EXTRA_UNIQUE_INDEX));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_extrauniq.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_extrauniq.db", true));

    /* 审计 round-5 B-1：partial unique index 不是全局唯一约束，但会拒绝合法写入，
       必须 fail-closed 拒绝这种库；同时确认原对象与数据未被补建/覆盖。 */
    TEST_CHECK(broken_schema_setup("struct_partial.db", SQL_PARTIAL_UNIQUE_INDEX));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_partial.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_partial.db", true));
    {
        char partial_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_partial.db", partial_path, sizeof(partial_path)));
        TEST_CHECK(fixture_object_count(partial_path, &objects));
        /* documents 表 + partial_collection 索引 + 三张真表不该被补建。 */
        TEST_CHECK(objects == 4);
    }

    /* 审计 round-6 B-1：官方三表齐全、列/键/metadata 全对，只多一个预存 RAISE(IGNORE)
       触发器。它不会改变任何可查结构，却能让命中 WHEN 的写入静默丢失，所以 open 必须按
       「对象清单恰好等于官方 DDL 产出」fail-closed 拒绝，且不能补建/删除原对象。 */
    TEST_CHECK(broken_schema_setup("struct_trigger.db", SQL_TRIGGER_IGNORE));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_trigger.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_trigger.db", true));
    {
        char trigger_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_trigger.db", trigger_path, sizeof(trigger_path)));
        TEST_CHECK(fixture_object_count(trigger_path, &objects));
        /* 三张官方表 + skip_second 触发器，一个不多一个不少（未被补建也未未被删除）。 */
        TEST_CHECK(objects == 4);
    }

    /* 审计 round-6 B-2：value_json 被声明成 VIRTUAL 生成列。pragma_table_info 直接漏掉它
       （只报两列），table_xinfo 的三列在列名/类型/NOT NULL/pk 上与官方完全一致，只有 hidden
       标志能区分；若不查 hidden 就会放行这种库，而它的 value_json 无法被正常写入。 */
    TEST_CHECK(broken_schema_setup("struct_generated.db", SQL_GENERATED_COLUMN));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_generated.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_generated.db", true));
    {
        char generated_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_generated.db", generated_path, sizeof(generated_path)));
        TEST_CHECK(fixture_object_count(generated_path, &objects));
        /* 生成列不新增 sqlite_master 对象，仍是三张表：证明拒的是隐藏列本身。 */
        TEST_CHECK(objects == 3);
    }

    /* 审计 round-6 B-2 的原始反例：末尾追加 VIRTUAL 生成列。table_info 仍报 3 列且列名/类型/
       NOT NULL/pk 全对，只有 table_xinfo 的 hidden 标志能拒；这条专门守住「换回 table_info」的回退。 */
    TEST_CHECK(broken_schema_setup("struct_generated_appended.db", SQL_GENERATED_APPENDED));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_generated_appended.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_generated_appended.db", true));
    {
        char appended_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_generated_appended.db", appended_path, sizeof(appended_path)));
        TEST_CHECK(fixture_object_count(appended_path, &objects));
        TEST_CHECK(objects == 3);
    }

    /* 审计 round-7：内联约束夹具。它们不新增用户对象（对象清单检查放行）、列结构与
       列级 collation 都合法，只有键索引检查能拒；用于让 verify_key_index 的每个分支
       都有独立可观测证据（⑧⑨⑩ 那类额外索引夹具会被排在键索引检查之后的对象清单检查
       一并拒掉，无法单独证明键索引分支）。 */
    TEST_CHECK(broken_schema_setup("struct_inline_extrauniq.db", SQL_INLINE_EXTRA_UNIQUE));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_inline_extrauniq.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_inline_extrauniq.db", true));

    TEST_CHECK(broken_schema_setup("struct_inline_pknocase.db", SQL_INLINE_PK_NOCASE));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_inline_pknocase.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_inline_pknocase.db", true));

    TEST_CHECK(broken_schema_setup("struct_inline_pkdesc.db", SQL_INLINE_PK_DESC));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_inline_pkdesc.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      broken_schema_probe("struct_inline_pkdesc.db", true));

    /* 审计 round-7 B-1：表内 CHECK / FOREIGN KEY 约束。它们不产生额外对象、不改列/
       索引/metadata/quick_check 视图，只有 DDL 文本比对能拒；同时确认原对象与原数据
       未被补建或清空。 */
    TEST_CHECK(broken_schema_setup("struct_check.db", SQL_CHECK_CONSTRAINT));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_check.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_check.db", true));
    {
        char check_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_check.db", check_path, sizeof(check_path)));
        TEST_CHECK(fixture_object_count(check_path, &objects));
        /* CHECK 不新增对象：仍是三张官方表，拒的是表定义本身。 */
        TEST_CHECK(objects == 3);
    }

    TEST_CHECK(broken_schema_setup("struct_foreign_key.db", SQL_FOREIGN_KEY));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_foreign_key.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_foreign_key.db", true));
    {
        char fk_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_foreign_key.db", fk_path, sizeof(fk_path)));
        TEST_CHECK(fixture_object_count(fk_path, &objects));
        TEST_CHECK(objects == 3);
    }

    /* 审计 round-7 B-1 补充：与官方 documents 文本等长但内容不同的等价写法（全小写）。
       长度校验放行、只有逐字节内容比较能拒；用于让「只比长度」的回退可观测。 */
    TEST_CHECK(broken_schema_setup("struct_same_len.db", SQL_DDL_SAME_LEN_DIFFERENT_TEXT));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_same_len.db", false));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_same_len.db", true));
    {
        char same_len_path[256];
        int objects = 0;
        TEST_CHECK(make_path("struct_same_len.db", same_len_path, sizeof(same_len_path)));
        TEST_CHECK(fixture_object_count(same_len_path, &objects));
        /* 小写等价定义不新增对象：仍是三张官方表。 */
        TEST_CHECK(objects == 3);
    }

    /* ⑦ 只有元数据表、documents 完全不存在：初始化路径也必须在 COMMIT 前核验三表结构，
       不能只看元数据（round-3 B-2 的另一半）。 */
    {
        char bare_path[256];
        TEST_CHECK(make_path("struct_bare.db", bare_path, sizeof(bare_path)));
        TEST_CHECK(write_text_file(bare_path, ""));
        sqlite3 *raw = NULL;
        TEST_CHECK(sqlite3_open(bare_path, &raw) == SQLITE_OK);
        if (raw != NULL) {
            TEST_CHECK(sqlite3_exec(raw,
                                    "CREATE TABLE meta(name TEXT PRIMARY KEY NOT NULL,"
                                    " value TEXT NOT NULL) WITHOUT ROWID;"
                                    "INSERT INTO meta(name,value) VALUES('schema_version','1');",
                                    NULL, NULL, NULL) == SQLITE_OK);
            sqlite3_close(raw);
        }
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_bare.db", true));
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, broken_schema_probe("struct_bare.db", false));
    }
}

/* 审计 round-6 B-1 第二道防线：open 之后才注入的触发器绕不过 verify_official_objects
   （它只在 open 时查对象清单），只能靠「语句执行后真的改了 1 行」来发现被静默吞掉的写入。
   这里在合法库上注入 skip_second，再 PUT 一个 key 命中 WHEN 的 tools 记录：
   RAISE(IGNORE) 让 UPSERT 一行都没动、且不报 SQL 错误，端口必须报 IO_ERROR 而不是成功，
   并且整单回滚（revision 不推进、文档不存在）。移除触发器后同一条 PUT 必须成功。 */
static void test_silently_ignored_write_detected(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("ignored.db", &repo));
    if (repo == NULL) {
        return;
    }

    char path[256];
    TEST_CHECK(make_path("ignored.db", path, sizeof(path)));
    sqlite3 *raw = NULL;
    TEST_CHECK(sqlite3_open(path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw, SQL_TRIGGER_IGNORE, NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }

    char tool[1024];
    build_tool_record(tool, sizeof(tool), "heat gun", 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, commit_put(repo, 0u, "tools", UUID_KEY, tool));

    /* 整单回滚：revision 仍是 0，key 也真的不存在（不是「写进去了但报了错」）。 */
    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_KEY, &result));
    TEST_CHECK_U64(0u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    /* 移除触发器后，同一条 PUT 必须成功并把 revision 推进到 1（证明失败来自触发器而非入参）。 */
    raw = NULL;
    TEST_CHECK(sqlite3_open(path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw, "DROP TRIGGER skip_second", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_KEY, tool));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_KEY, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);

    /* DELETE 侧同样要有 changes 校验：预存的 BEFORE DELETE + RAISE(IGNORE) 会让「本来存在
       的 key」一行都没删掉却不报错。删除不存在的 key 是合法空操作，所以判据是
       「existed && changes != 1」而不是无条件 changes == 1。 */
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("ignored_delete.db", &repo));
    if (repo == NULL) {
        return;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_KEY, tool));

    char delete_path[256];
    TEST_CHECK(make_path("ignored_delete.db", delete_path, sizeof(delete_path)));
    raw = NULL;
    TEST_CHECK(sqlite3_open(delete_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw,
                                "CREATE TRIGGER skip_delete BEFORE DELETE ON documents"
                                " WHEN OLD.key = '" UUID_KEY "'"
                                " BEGIN SELECT RAISE(IGNORE); END;",
                                NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }

    char delete_mutation[512];
    char delete_change[1024];
    TEST_CHECK(build_delete(delete_mutation, sizeof(delete_mutation), "tools", UUID_KEY) > 0);
    TEST_CHECK(build_change(delete_change, sizeof(delete_change), 1u, "[]", delete_mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, commit_json(repo, delete_change));

    /* 整单回滚：revision 仍是 1，文档仍在。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_KEY, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 1u);
        cJSON_Delete(root);
    }

    /* 删除不存在的 key 仍是合法空操作（不能因为 changes==0 就报错）。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, "{\"schema_version\":1,"
                                                   "\"expected_snapshot_revision\":\"1\","
                                                   "\"new_snapshot_revision\":\"2\","
                                                   "\"checks\":[],\"mutations\":[{\"op\":\"DELETE\","
                                                   "\"collection\":\"tools\","
                                                   "\"key\":\"" UUID_TOOL "\"}]}"));

    /* 移除触发器后同一条 DELETE 必须成功（revision 从 2 推进到 3）。 */
    raw = NULL;
    TEST_CHECK(sqlite3_open(delete_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw, "DROP TRIGGER skip_delete", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }
    TEST_CHECK(build_change(delete_change, sizeof(delete_change), 2u, "[]", delete_mutation) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, delete_change));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_key(repo, "tools", UUID_KEY, &result));
    TEST_CHECK_U64(3u, result.snapshot_revision);
    root = parse_result(&result);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 0u);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);
}


/* ---------- 10c. 官方 DDL 正对照（审计 round-7 B-1） ---------- */

/* 正对照必须提交「坏库会失败的那一份输入」才成立（审计 round-8 N-3）：
   AuditRecord 的 key 与 value.id 相同，且第二条 key 正好是 CHECK 夹具谓词里的
   UUID_KEY。此前正对照用手填 key 000000000001/000000000002，根本没命中
   CHECK(key <> UUID_KEY) 谓词，因此不能声称它与坏库失败的是同一份写入。 */
static void build_audit_record_with_id(char *out, size_t out_size, const char *id, const char *reason)
{
    snprintf(out, out_size,
             "{\"id\":\"%s\",\"actor_id\":null,"
             "\"operation_id\":null,\"type\":\"PAGE_BUDGET\",\"reason\":\"%s\","
             "\"occurred_at\":null,\"time_quality\":\"HOST_SYNCED\",\"source_event\":null}",
             id, reason);
}

/* DDL 文本比对必须只拒绝被改过的库，不能把官方 schema-v1.sql 建出的库也拒掉。
   这里用唯一官方 DDL 建库，再提交「同一份 change 里两条 audits」——正是 B-1 反例
   在坏库上会失败的那份写入——证明放行侧没有被过度收紧。 */
static void test_official_ddl_positive_control(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("ddl_ok.db", &repo));
    if (repo == NULL) {
        return;
    }

    char first_record[2048];
    char second_record[2048];
    char first[4096];
    char second[4096];
    char mutations[8192];
    char change[16384];
    static const char *const k_first_id = "11111111-2222-4333-8444-555555555555";
    build_audit_record_with_id(first_record, sizeof(first_record), k_first_id,
                               "positive control one");
    build_audit_record_with_id(second_record, sizeof(second_record), UUID_KEY,
                               "positive control two");
    /* key 必须等于 value.id，第二条还必须等于 CHECK 谓词里的 UUID_KEY：
       这两条断言保证正对照输入与 ⑯ 坏库反例是同一种写入。 */
    TEST_CHECK(strstr(first_record, k_first_id) != NULL);
    TEST_CHECK(strstr(second_record, UUID_KEY) != NULL);
    TEST_CHECK(build_put(first, sizeof(first), "audits", k_first_id, first_record) > 0);
    TEST_CHECK(build_put(second, sizeof(second), "audits", UUID_KEY, second_record) > 0);
    TEST_CHECK(snprintf(mutations, sizeof(mutations), "%s,%s", first, second) > 0);
    TEST_CHECK(build_change(change, sizeof(change), 0u, "[]", mutations) > 0);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_json(repo, change));

    locker_repo_result_t result;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, query_page(repo, "audits", "", 10u, &result));
    TEST_CHECK_U64(1u, result.snapshot_revision);
    cJSON *root = parse_result(&result);
    TEST_CHECK(root != NULL);
    if (root != NULL) {
        TEST_CHECK(documents_count(root) == 2u);
        cJSON_Delete(root);
    }

    locker_repo_close(repo);
}

/* ---------- 11. 打开失败路径 ---------- */

static bool write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    size_t length = strlen(text);
    bool ok = fwrite(text, 1u, length, file) == length;
    fclose(file);
    return ok;
}

static void test_open_failure_paths(void)
{
    /* 损坏文件：打开必须失败，且不能删库/清库。 */
    char corrupt[256];
    TEST_CHECK(make_path("corrupt.db", corrupt, sizeof(corrupt)));
    TEST_CHECK(write_text_file(corrupt, "this is not a sqlite database, keep me"));
    locker_repo_t *repo = NULL;
    locker_status_t status = open_repo_at(corrupt, false, &repo);
    TEST_CHECK(status != LOCKER_STATUS_OK);
    TEST_CHECK(repo == NULL);
    FILE *file = fopen(corrupt, "rb");
    TEST_CHECK(file != NULL);
    if (file != NULL) {
        char buffer[64];
        size_t read_bytes = fread(buffer, 1u, sizeof(buffer) - 1u, file);
        buffer[read_bytes] = '\0';
        TEST_CHECK(strcmp(buffer, "this is not a sqlite database, keep me") == 0);
        fclose(file);
    }
    /* initialize_empty 也不能把一个垃圾文件当成新库覆盖掉。 */
    repo = NULL;
    status = open_repo_at(corrupt, true, &repo);
    TEST_CHECK(status != LOCKER_STATUS_OK);
    TEST_CHECK(repo == NULL);

    /* 元数据被篡改（schema_version=2）→ CONFLICT，不自动清库。 */
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("tamper.db", &repo));
    locker_repo_close(repo);
    char tamper_path[256];
    TEST_CHECK(make_path("tamper.db", tamper_path, sizeof(tamper_path)));
    sqlite3 *raw = NULL;
    TEST_CHECK(sqlite3_open(tamper_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw, "UPDATE meta SET value = '2' WHERE name = 'schema_version'", NULL,
                                NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(tamper_path, false, &repo));
    TEST_CHECK(repo == NULL);

    /* 迁移记录缺失 → CONFLICT。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("nomigration.db", &repo));
    locker_repo_close(repo);
    char migration_path[256];
    TEST_CHECK(make_path("nomigration.db", migration_path, sizeof(migration_path)));
    raw = NULL;
    TEST_CHECK(sqlite3_open(migration_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw, "DELETE FROM migrations", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(migration_path, false, &repo));
    TEST_CHECK(repo == NULL);

    /* 只有 meta、没有 documents 表的「半初始化」库（审计 B3）：
       这是迁移/初始化中途失败的痕迹，不是「尚未初始化」的空目标，
       必须 CONFLICT 而不是被 CREATE TABLE IF NOT EXISTS + INSERT OR IGNORE 悄悄补齐。 */
    char bare_path[256];
    TEST_CHECK(make_path("bare.db", bare_path, sizeof(bare_path)));
    raw = NULL;
    TEST_CHECK(sqlite3_open(bare_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        TEST_CHECK(sqlite3_exec(raw,
                                "CREATE TABLE meta (name TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);"
                                "INSERT INTO meta(name, value) VALUES ('schema_version', '1');",
                                NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(raw);
    }
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(bare_path, false, &repo));
    TEST_CHECK(repo == NULL);
    /* 带 initialize_empty 也不能把半初始化库当新库重建。 */
    repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, open_repo_at(bare_path, true, &repo));
    TEST_CHECK(repo == NULL);
    /* 半初始化库必须原样保留：没有新增 migrations/documents 表。 */
    raw = NULL;
    TEST_CHECK(sqlite3_open(bare_path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(raw,
                               "SELECT count(*) FROM sqlite_master WHERE type = 'table'"
                               " AND name NOT LIKE 'sqlite_%'",
                               -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                TEST_CHECK(sqlite3_column_int(stmt, 0) == 1);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(raw);
    }
}

/* ---------- 12. WAL ---------- */

static void test_wal_enabled(void)
{
    locker_repo_t *repo = NULL;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, open_new_repo("wal.db", &repo));
    if (repo == NULL) {
        return;
    }
    char tool[1024];
    build_tool_record(tool, sizeof(tool), "power supply", 1u);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, commit_put(repo, 0u, "tools", UUID_TOOL, tool));

    /* journal_mode 是持久化属性：另开一个连接读回来必须是 wal。 */
    char path[256];
    TEST_CHECK(make_path("wal.db", path, sizeof(path)));
    sqlite3 *raw = NULL;
    TEST_CHECK(sqlite3_open(path, &raw) == SQLITE_OK);
    if (raw != NULL) {
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(raw, "PRAGMA journal_mode", -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char *mode = sqlite3_column_text(stmt, 0);
                TEST_CHECK(mode != NULL && strcmp((const char *)mode, "wal") == 0);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(raw);
    }

    /* synchronous=FULL 是连接级 pragma，无法从另一个连接观测；由 locker_repo_open
       的代码路径与 handoff 说明保证，不在这里伪造运行时断言。 */

    /* WAL 生效时写入会落 -wal 文件（连接未关闭期间）。 */
    char wal_path[300];
    int written = snprintf(wal_path, sizeof(wal_path), "%s-wal", path);
    TEST_CHECK(written > 0 && (size_t)written < sizeof(wal_path));
    FILE *wal = fopen(wal_path, "rb");
    TEST_CHECK(wal != NULL);
    if (wal != NULL) {
        fclose(wal);
    }

    locker_repo_close(repo);
}

int main(void)
{
    char template_path[] = "/tmp/locker-s102-XXXXXX";
    char *created = mkdtemp(template_path);
    TEST_CHECK(created != NULL);
    if (created == NULL) {
        TEST_REPORT("storage.repository");
    }
    snprintf(g_dir, sizeof(g_dir), "%s", created);

    test_open_and_initialize();
    test_commit_put_and_query();
    test_revision_conflict();
    test_failed_transaction_is_atomic();
    test_competing_slot_guard();
    test_concurrent_slot_guard();
    test_persistence_across_reopen();
    test_import_events_dedup();
    test_import_events_gap_and_regression();
    test_import_page_boundaries();
    test_import_boundary_envelope();
    test_import_continuity_latching();
    test_boot_jump_keeps_continuity();
    test_duplicate_page_latches_gap();
    test_import_backfill_rejected();
    test_cursor_order_and_pagination();
    test_recovery_pages();
    test_pagination_budget();
    test_embedded_nul_rejected();
    test_open_failure_paths();
    test_documents_view_rejected();
    test_sqlite_prefixed_user_object_rejected();
    test_schema_structure_verified();
    test_official_ddl_positive_control();
    test_silently_ignored_write_detected();
    test_wal_enabled();

    /* 清理测试库：只删本进程创建的临时目录内容。 */
    char command[256];
    int written = snprintf(command, sizeof(command), "rm -rf '%s'", g_dir);
    TEST_CHECK(written > 0 && (size_t)written < sizeof(command));
    if (written > 0 && (size_t)written < sizeof(command)) {
        const int rc = system(command);
        (void)rc;
    }

    TEST_REPORT("storage.repository");
}
