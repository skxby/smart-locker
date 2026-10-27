#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <string.h>

#include "locker/storage/schema.h"

static locker_storage_schema_t *g_schema;

static bool validate(const char *def_name, const char *json)
{
    return locker_storage_schema_validate_json(g_schema, def_name, json, strlen(json));
}

static bool validate_collection(const char *collection, const char *json)
{
    size_t index = 0u;
    if (!locker_storage_schema_collection_lookup(collection, strlen(collection), &index)) {
        return false;
    }
    return locker_storage_schema_validate_document_json(g_schema, index, json, strlen(json));
}

static void test_collections(void)
{
    TEST_CHECK(locker_storage_schema_collection_name(0u) != NULL);
    TEST_CHECK_STRING("audits", locker_storage_schema_collection_name(0u));
    TEST_CHECK_STRING("device_events", locker_storage_schema_collection_name(3u));
    TEST_CHECK_STRING("import_cursors", locker_storage_schema_collection_name(6u));
    TEST_CHECK_STRING("vision_operations", locker_storage_schema_collection_name(17u));
    TEST_CHECK(locker_storage_schema_collection_name(18u) == NULL);

    static const char *const expected[LOCKER_STORAGE_COLLECTION_COUNT] = {
        "audits",          "card_uid_index", "cards",           "device_events",
        "device_state",    "idempotency",    "import_cursors",  "loans",
        "operations",      "outbox",         "sessions",        "slot_guards",
        "tools",           "username_index", "users",           "vision_captures",
        "vision_evidence", "vision_operations"};
    for (size_t index = 0u; index < LOCKER_STORAGE_COLLECTION_COUNT; index++) {
        size_t found = 999u;
        TEST_CHECK(locker_storage_schema_collection_lookup(expected[index], strlen(expected[index]), &found));
        TEST_CHECK(found == index);
    }

    size_t found = 999u;
    TEST_CHECK(!locker_storage_schema_collection_lookup("nope", 4u, &found));
    TEST_CHECK(!locker_storage_schema_collection_lookup("users", 4u, &found));
    TEST_CHECK(!locker_storage_schema_collection_lookup("", 0u, &found));
    TEST_CHECK(!locker_storage_schema_collection_lookup(NULL, 4u, &found));
    TEST_CHECK(!locker_storage_schema_collection_lookup("users", 5u, NULL));
}

static void test_user_record(void)
{
    const char *good =
        "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
        "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,\"user_revision\":\"3\"},"
        "\"password_hash\":\"$argon2id$v=19$m=65536,t=3,p=1$c2FsdA$aGFzaA\"}";
    TEST_CHECK(validate("UserRecord", good));
    TEST_CHECK(validate_collection("users", good));

    /* 缺少 required 字段必须失败。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"3\"}}"));
    /* 多出未知字段必须失败（additionalProperties:false）。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"3\",\"extra\":1},\"password_hash\":\"x\"}"));
    /* role 不在枚举里必须失败。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"ROOT\",\"enabled\":true,"
                         "\"user_revision\":\"3\"},\"password_hash\":\"x\"}"));
    /* u64 必须是十进制字符串：数字类型、前导零、负数都失败。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":3},\"password_hash\":\"x\"}"));
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"03\"},\"password_hash\":\"x\"}"));
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"18446744073709551616\"},\"password_hash\":\"x\"}"));
    /* uuid 必须是小写规范形态。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"111111112222433384445555555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"3\"},\"password_hash\":\"x\"}"));
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"AAAAAAAA-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"3\"},\"password_hash\":\"x\"}"));
    /* username 长度边界：32 通过、33 失败、空失败。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"3\"},\"password_hash\":\"x\"}"));
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"username\":\"0123456789012345678901234567890123\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":true,"
                         "\"user_revision\":\"3\"},\"password_hash\":\"x\"}"));
    /* enabled 必须是 bool，不能是 0/1。 */
    TEST_CHECK(!validate("UserRecord",
                         "{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"username\":\"alice\","
                         "\"display_name\":\"Alice\",\"role\":\"USER\",\"enabled\":1,"
                         "\"user_revision\":\"3\"},\"password_hash\":\"x\"}"));
    /* 非法 JSON 必须失败。 */
    TEST_CHECK(!validate("UserRecord", "{}"));
    TEST_CHECK(!validate("UserRecord", "not json"));
    TEST_CHECK(!validate("UserRecord", ""));
}

static void test_session_and_card(void)
{
    TEST_CHECK(validate("SessionRecord",
                        "{\"id\":\"11111111-2222-4333-8444-555555555555\","
                        "\"user_id\":\"22222222-2222-4333-8444-555555555555\","
                        "\"token_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
                        "\"expires_at\":\"2026-10-24T00:00:00Z\",\"revoked\":false}"));
    /* token_hash 必须正好 64 位小写 hex。 */
    TEST_CHECK(!validate("SessionRecord",
                         "{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"user_id\":\"22222222-2222-4333-8444-555555555555\","
                         "\"token_hash\":\"0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef\","
                         "\"expires_at\":\"2026-10-24T00:00:00Z\",\"revoked\":false}"));
    TEST_CHECK(!validate("SessionRecord",
                         "{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"user_id\":\"22222222-2222-4333-8444-555555555555\","
                         "\"token_hash\":\"abcd\",\"expires_at\":\"2026-10-24T00:00:00Z\","
                         "\"revoked\":false}"));

    TEST_CHECK(validate("CardRecord",
                        "{\"user_id\":\"11111111-2222-4333-8444-555555555555\","
                        "\"uid_hex\":\"04a2b3c4\",\"bound_at\":\"2026-10-24T00:00:00Z\"}"));
    TEST_CHECK(validate("CardRecord",
                        "{\"user_id\":\"11111111-2222-4333-8444-555555555555\","
                        "\"uid_hex\":\"0123456789abcd\",\"bound_at\":\"2026-10-24T00:00:00Z\"}"));
    TEST_CHECK(validate("CardRecord",
                        "{\"user_id\":\"11111111-2222-4333-8444-555555555555\","
                        "\"uid_hex\":\"0123456789abcdef0123\",\"bound_at\":\"2026-10-24T00:00:00Z\"}"));
    /* 卡 UID 只允许 8/14/20 位小写 hex。 */
    TEST_CHECK(!validate("CardRecord",
                         "{\"user_id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"uid_hex\":\"04a2b3c\",\"bound_at\":\"2026-10-24T00:00:00Z\"}"));
    TEST_CHECK(!validate("CardRecord",
                         "{\"user_id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"uid_hex\":\"04A2B3C4\",\"bound_at\":\"2026-10-24T00:00:00Z\"}"));
    TEST_CHECK(!validate("CardRecord",
                         "{\"user_id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"uid_hex\":\"04a2b3c4\",\"bound_at\":\"2026-10-24 00:00:00\"}"));
}

static void test_tool_and_slot_guard(void)
{
    TEST_CHECK(validate("ToolRecord",
                        "{\"id\":\"11111111-2222-4333-8444-555555555555\",\"name\":\"heat gun\","
                        "\"slot_id\":1,\"tool_revision\":\"1\",\"enabled\":true}"));
    /* slot_id 冻结为 1（单柜单格）。 */
    TEST_CHECK(!validate("ToolRecord",
                         "{\"id\":\"11111111-2222-4333-8444-555555555555\",\"name\":\"heat gun\","
                         "\"slot_id\":2,\"tool_revision\":\"1\",\"enabled\":true}"));
    TEST_CHECK(!validate("ToolRecord",
                         "{\"id\":\"11111111-2222-4333-8444-555555555555\",\"name\":\"\","
                         "\"slot_id\":1,\"tool_revision\":\"1\",\"enabled\":true}"));

    TEST_CHECK(validate("SlotGuardRecord",
                        "{\"slot_id\":1,\"operation_id\":\"11111111-2222-4333-8444-555555555555\"}"));
    TEST_CHECK(!validate("SlotGuardRecord",
                         "{\"slot_id\":1,\"operation_id\":\"11111111-2222-4333-8444-55555555555\"}"));
    TEST_CHECK(validate("IndexRecord",
                        "{\"value\":\"alice\",\"resource_id\":\"11111111-2222-4333-8444-555555555555\"}"));
}

static void test_event_and_cursor(void)
{
    const char *event_key =
        "{\"device_id\":\"0123456789abcdef01234567\","
        "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
        "\"boot_id\":\"1\",\"event_seq\":\"2\"}";
    TEST_CHECK(validate("EventKey", event_key));
    TEST_CHECK(!validate("EventKey",
                         "{\"device_id\":\"0123456789ABCDEF01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"boot_id\":\"1\",\"event_seq\":\"2\"}"));

    const char *cursor =
        "{\"device_id\":\"0123456789abcdef01234567\","
        "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
        "\"through\":{\"device_id\":\"0123456789abcdef01234567\","
        "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
        "\"boot_id\":\"1\",\"event_seq\":\"9\"},\"continuity_ok\":true}";
    TEST_CHECK(validate("ImportCursorRecord", cursor));
    TEST_CHECK(validate_collection("import_cursors", cursor));
    TEST_CHECK(!validate("ImportCursorRecord",
                         "{\"device_id\":\"0123456789abcdef01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"through\":{\"device_id\":\"0123456789abcdef01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"boot_id\":\"1\",\"event_seq\":\"9\"}}"));

    const char *event =
        "{\"key\":{\"device_id\":\"0123456789abcdef01234567\","
        "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
        "\"boot_id\":\"1\",\"event_seq\":\"1\"},"
        "\"mono_ms\":\"1000\",\"physical_revision\":\"0\",\"event_type\":1,\"reason\":0,"
        "\"operation_id\":null,\"command_id\":null,\"door\":\"CLOSED\",\"item\":\"PRESENT\","
        "\"flags\":0,\"supply_mv\":null,\"detail\":\"0\",\"card_uid_hex\":null}";
    TEST_CHECK(validate("DeviceEventRecord", event));
    TEST_CHECK(validate_collection("device_events", event));
    /* event_type 只允许 1..15、reason 0..14、flags 0..65535。 */
    TEST_CHECK(!validate("DeviceEventRecord",
                         "{\"key\":{\"device_id\":\"0123456789abcdef01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"boot_id\":\"1\",\"event_seq\":\"1\"},"
                         "\"mono_ms\":\"1000\",\"physical_revision\":\"0\",\"event_type\":0,"
                         "\"reason\":0,\"operation_id\":null,\"command_id\":null,\"door\":\"CLOSED\","
                         "\"item\":\"PRESENT\",\"flags\":0,\"supply_mv\":null,\"detail\":\"0\","
                         "\"card_uid_hex\":null}"));
    TEST_CHECK(!validate("DeviceEventRecord",
                         "{\"key\":{\"device_id\":\"0123456789abcdef01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"boot_id\":\"1\",\"event_seq\":\"1\"},"
                         "\"mono_ms\":\"1000\",\"physical_revision\":\"0\",\"event_type\":1,"
                         "\"reason\":0,\"operation_id\":null,\"command_id\":null,\"door\":\"CLOSED\","
                         "\"item\":\"PRESENT\",\"flags\":65536,\"supply_mv\":null,\"detail\":\"0\","
                         "\"card_uid_hex\":null}"));
}

static void test_repository_change(void)
{
    const char *good =
        "{\"schema_version\":1,\"expected_snapshot_revision\":\"0\","
        "\"new_snapshot_revision\":\"1\",\"checks\":[],\"mutations\":["
        "{\"op\":\"PUT\",\"document\":{\"collection\":\"tools\","
        "\"key\":\"11111111-2222-4333-8444-555555555555\","
        "\"value\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"name\":\"heat gun\","
        "\"slot_id\":1,\"tool_revision\":\"1\",\"enabled\":true}}}]}";
    TEST_CHECK(validate("RepositoryChange", good));
    TEST_CHECK(validate("RepoDocument",
                        "{\"collection\":\"tools\",\"key\":\"11111111-2222-4333-8444-555555555555\","
                        "\"value\":{\"id\":\"11111111-2222-4333-8444-555555555555\",\"name\":\"heat gun\","
                        "\"slot_id\":1,\"tool_revision\":\"1\",\"enabled\":true}}"));

    /* mutations 为空必须失败（minItems 1）。 */
    TEST_CHECK(!validate("RepositoryChange",
                         "{\"schema_version\":1,\"expected_snapshot_revision\":\"0\","
                         "\"new_snapshot_revision\":\"1\",\"checks\":[],\"mutations\":[]}"));
    /* schema_version 必须是 const 1。 */
    TEST_CHECK(!validate("RepositoryChange",
                         "{\"schema_version\":2,\"expected_snapshot_revision\":\"0\","
                         "\"new_snapshot_revision\":\"1\",\"checks\":[],\"mutations\":["
                         "{\"op\":\"DELETE\",\"collection\":\"tools\","
                         "\"key\":\"11111111-2222-4333-8444-555555555555\"}]}"));
    /* 未知 collection 必须失败。 */
    TEST_CHECK(!validate("RepoCheck",
                         "{\"collection\":\"nope\",\"key\":\"a\",\"condition\":\"ABSENT\"}"));
    TEST_CHECK(validate("RepoCheck",
                        "{\"collection\":\"slot_guards\",\"key\":\"1\",\"condition\":\"ABSENT\"}"));
    /* 未知 op 必须失败（anyOf 两个分支都不匹配）。 */
    TEST_CHECK(!validate("RepoMutation",
                         "{\"op\":\"PATCH\",\"collection\":\"tools\",\"key\":\"k\"}"));
    /* PUT 分支必须带 document，DELETE 分支不能带 document。 */
    TEST_CHECK(!validate("RepoMutation",
                         "{\"op\":\"PUT\",\"collection\":\"tools\",\"key\":\"k\"}"));
    TEST_CHECK(!validate("RepoMutation",
                         "{\"op\":\"DELETE\",\"collection\":\"tools\",\"key\":\"k\","
                         "\"document\":{\"collection\":\"tools\",\"key\":\"k\","
                         "\"value\":{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"name\":\"x\",\"slot_id\":1,\"tool_revision\":\"1\","
                         "\"enabled\":true}}}"));
    /* document 的 collection 与 value 必须同型：tools 里放 UserRecord 必须失败。 */
    TEST_CHECK(!validate("RepoDocument",
                         "{\"collection\":\"tools\",\"key\":\"k\","
                         "\"value\":{\"user\":{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"username\":\"alice\",\"display_name\":\"Alice\",\"role\":\"USER\","
                         "\"enabled\":true,\"user_revision\":\"1\"},\"password_hash\":\"x\"}}"));
}

static void test_query_dto(void)
{
    TEST_CHECK(validate("RepositoryQuery",
                        "{\"schema_version\":1,\"collection\":\"users\","
                        "\"key\":\"11111111-2222-4333-8444-555555555555\","
                        "\"after_key\":null,\"limit\":1}"));
    TEST_CHECK(validate("RepositoryQuery",
                        "{\"schema_version\":1,\"collection\":\"users\",\"key\":null,"
                        "\"after_key\":null,\"limit\":100}"));
    TEST_CHECK(validate("RepositoryQuery",
                        "{\"schema_version\":1,\"collection\":\"users\",\"key\":null,"
                        "\"after_key\":\"abc\",\"limit\":1}"));
    /* key 与 after_key 互斥（x-rules 09）：两个都给必须失败。 */
    TEST_CHECK(!validate("RepositoryQuery",
                         "{\"schema_version\":1,\"collection\":\"users\","
                         "\"key\":\"11111111-2222-4333-8444-555555555555\","
                         "\"after_key\":\"abc\",\"limit\":1}"));
    /* 按键分支 limit 冻结为 1。 */
    TEST_CHECK(!validate("RepositoryQuery",
                         "{\"schema_version\":1,\"collection\":\"users\","
                         "\"key\":\"11111111-2222-4333-8444-555555555555\","
                         "\"after_key\":null,\"limit\":2}"));
    /* 分页 limit 上限 100。 */
    TEST_CHECK(!validate("RepositoryQuery",
                         "{\"schema_version\":1,\"collection\":\"users\",\"key\":null,"
                         "\"after_key\":null,\"limit\":101}"));
    TEST_CHECK(!validate("RepositoryQuery",
                         "{\"schema_version\":1,\"collection\":\"users\",\"key\":null,"
                         "\"after_key\":null,\"limit\":0}"));

    TEST_CHECK(validate("RepositoryQueryResult",
                        "{\"schema_version\":1,\"snapshot_revision\":\"0\",\"documents\":[],"
                        "\"next_after_key\":null}"));
    TEST_CHECK(!validate("RepositoryQueryResult",
                         "{\"schema_version\":1,\"snapshot_revision\":\"0\",\"documents\":[]}"));
    TEST_CHECK(validate("RepositoryRecoveryPage",
                        "{\"schema_version\":1,\"snapshot_revision\":\"0\",\"collection\":\"loans\","
                        "\"documents\":[],\"next_after_key\":null}"));
}

static void test_unknown_keyword_and_unknown_def(void)
{
    TEST_CHECK(!validate("NoSuchDef", "{}"));
    TEST_CHECK(!locker_storage_schema_validate_json(NULL, "UserRecord", "{}", 2u));
    TEST_CHECK(!locker_storage_schema_validate_json(g_schema, NULL, "{}", 2u));
    TEST_CHECK(!locker_storage_schema_validate_json(g_schema, "UserRecord", NULL, 2u));
    TEST_CHECK(!locker_storage_schema_validate_json(g_schema, "UserRecord", "", 0u));
    TEST_CHECK(!locker_storage_schema_validate_document_json(g_schema, 99u, "{}", 2u));
    TEST_CHECK(!locker_storage_schema_validate_document_json(g_schema, 0u, "", 0u));
}

static void test_operation_record(void)
{
    const char *snapshot =
        "{\"device_id\":\"0123456789abcdef01234567\","
        "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
        "\"boot_id\":\"1\",\"physical_revision\":\"4\",\"door\":\"CLOSED\","
        "\"item\":\"PRESENT\",\"supply_mv\":4980,\"observed_age_ms\":120,"
        "\"time_quality\":\"HOST_SYNCED\",\"continuity_ok\":true,\"dropped_events\":\"0\","
        "\"durable_cursor\":{\"device_id\":\"0123456789abcdef01234567\","
        "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
        "\"boot_id\":\"1\",\"event_seq\":\"9\"}}";
    TEST_CHECK(validate("PhysicalSnapshot", snapshot));
    TEST_CHECK(!validate("PhysicalSnapshot",
                         "{\"device_id\":\"0123456789abcdef01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"boot_id\":\"1\",\"physical_revision\":\"4\",\"door\":\"CLOSED\","
                         "\"item\":\"PRESENT\",\"supply_mv\":100001,\"observed_age_ms\":120,"
                         "\"time_quality\":\"HOST_SYNCED\",\"continuity_ok\":true,\"dropped_events\":\"0\","
                         "\"durable_cursor\":{\"device_id\":\"0123456789abcdef01234567\","
                         "\"journal_epoch\":\"11111111-2222-4333-8444-555555555555\","
                         "\"boot_id\":\"1\",\"event_seq\":\"9\"}}"));

    /* OperationRecord.baseline 是 $ref PhysicalSnapshot，不是可空字段（真源 required + 无 null 分支），
       因此这里必须给一个真实快照，不能用 null 蒙过去。 */
    char record[4096];
    int written = snprintf(record, sizeof(record),
                           "{\"operation\":{\"id\":\"11111111-2222-4333-8444-555555555555\","
                           "\"tool_id\":\"22222222-2222-4333-8444-555555555555\",\"kind\":\"BORROW\","
                           "\"actor_id\":\"33333333-2222-4333-8444-555555555555\",\"state\":\"REQUESTED\","
                           "\"operation_revision\":\"1\",\"command_id\":null,\"reason_code\":null,"
                           "\"created_at\":\"2026-10-24T00:00:00Z\",\"updated_at\":\"2026-10-24T00:00:00Z\","
                           "\"allowed_actions\":[\"PRESENT_AT_CABINET\"],"
                           "\"evidence\":{\"history_complete\":true,\"door_open_seen\":false,"
                           "\"item_transition_seen\":false,\"physical\":null,\"source_events\":[],"
                           "\"manual_resolution\":false}},"
                           "\"baseline\":%s,\"related_loan_id\":null,"
                           "\"host_instance_id\":\"44444444-2222-4333-8444-555555555555\","
                           "\"proof_source\":\"CARD\",\"proof_consumed\":false}",
                           snapshot);
    TEST_CHECK(written > 0 && (size_t)written < sizeof(record));
    TEST_CHECK(validate("OperationRecord", record));
    TEST_CHECK(validate_collection("operations", record));

    /* baseline 为 null 必须失败：真源里它不是 anyOf[..., null]。 */
    TEST_CHECK(!validate("OperationRecord",
                         "{\"operation\":{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"tool_id\":\"22222222-2222-4333-8444-555555555555\",\"kind\":\"BORROW\","
                         "\"actor_id\":\"33333333-2222-4333-8444-555555555555\",\"state\":\"REQUESTED\","
                         "\"operation_revision\":\"1\",\"command_id\":null,\"reason_code\":null,"
                         "\"created_at\":\"2026-10-24T00:00:00Z\",\"updated_at\":\"2026-10-24T00:00:00Z\","
                         "\"allowed_actions\":[],"
                         "\"evidence\":{\"history_complete\":true,\"door_open_seen\":false,"
                         "\"item_transition_seen\":false,\"physical\":null,\"source_events\":[],"
                         "\"manual_resolution\":false}},"
                         "\"baseline\":null,\"related_loan_id\":null,"
                         "\"host_instance_id\":\"44444444-2222-4333-8444-555555555555\","
                         "\"proof_source\":\"CARD\",\"proof_consumed\":false}"));

    /* state 必须是 9 个枚举之一（baseline 合法，失败只能来自 state）。 */
    char bad_state[4096];
    written = snprintf(bad_state, sizeof(bad_state),
                       "{\"operation\":{\"id\":\"11111111-2222-4333-8444-555555555555\","
                       "\"tool_id\":\"22222222-2222-4333-8444-555555555555\",\"kind\":\"BORROW\","
                       "\"actor_id\":\"33333333-2222-4333-8444-555555555555\",\"state\":\"DONE\","
                       "\"operation_revision\":\"1\",\"command_id\":null,\"reason_code\":null,"
                       "\"created_at\":\"2026-10-24T00:00:00Z\",\"updated_at\":\"2026-10-24T00:00:00Z\","
                       "\"allowed_actions\":[],"
                       "\"evidence\":{\"history_complete\":true,\"door_open_seen\":false,"
                       "\"item_transition_seen\":false,\"physical\":null,\"source_events\":[],"
                       "\"manual_resolution\":false}},"
                       "\"baseline\":%s,\"related_loan_id\":null,"
                       "\"host_instance_id\":\"44444444-2222-4333-8444-555555555555\","
                       "\"proof_source\":\"CARD\",\"proof_consumed\":false}",
                       snapshot);
    TEST_CHECK(written > 0 && (size_t)written < sizeof(bad_state));
    TEST_CHECK(!validate("OperationRecord", bad_state));

    /* allowed_actions 上限 6。 */
    TEST_CHECK(!validate("Operation",
                         "{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"tool_id\":\"22222222-2222-4333-8444-555555555555\",\"kind\":\"BORROW\","
                         "\"actor_id\":\"33333333-2222-4333-8444-555555555555\",\"state\":\"REQUESTED\","
                         "\"operation_revision\":\"1\",\"command_id\":null,\"reason_code\":null,"
                         "\"created_at\":\"2026-10-24T00:00:00Z\",\"updated_at\":\"2026-10-24T00:00:00Z\","
                         "\"allowed_actions\":[\"WAIT\",\"WAIT\",\"WAIT\",\"WAIT\",\"WAIT\",\"WAIT\",\"WAIT\"],"
                         "\"evidence\":{\"history_complete\":true,\"door_open_seen\":false,"
                         "\"item_transition_seen\":false,\"physical\":null,\"source_events\":[],"
                         "\"manual_resolution\":false}}"));
}

static void test_outbox_and_idempotency(void)
{
    TEST_CHECK(validate("OutboxRecord",
                        "{\"id\":\"11111111-2222-4333-8444-555555555555\","
                        "\"operation_id\":\"22222222-2222-4333-8444-555555555555\","
                        "\"session_id\":\"33333333-2222-4333-8444-555555555555\","
                        "\"journal_epoch\":\"44444444-2222-4333-8444-555555555555\","
                        "\"expected_boot_id\":\"1\",\"expected_physical_revision\":\"4\","
                        "\"action_no\":\"1\",\"not_after_mcu_ms\":\"9000\",\"kind\":\"BORROW\","
                        "\"slot_id\":1,\"state\":\"PREPARED\",\"attempted_at\":null}"));
    TEST_CHECK(!validate("OutboxRecord",
                         "{\"id\":\"11111111-2222-4333-8444-555555555555\","
                         "\"operation_id\":\"22222222-2222-4333-8444-555555555555\","
                         "\"session_id\":\"33333333-2222-4333-8444-555555555555\","
                         "\"journal_epoch\":\"44444444-2222-4333-8444-555555555555\","
                         "\"expected_boot_id\":\"1\",\"expected_physical_revision\":\"4\","
                         "\"action_no\":\"1\",\"not_after_mcu_ms\":\"9000\",\"kind\":\"BORROW\","
                         "\"slot_id\":1,\"state\":\"SENT\",\"attempted_at\":null}"));

    TEST_CHECK(validate("IdempotencyRecord",
                        "{\"actor_id\":\"11111111-2222-4333-8444-555555555555\",\"method\":\"POST\","
                        "\"path\":\"/operations\",\"key\":\"22222222-2222-4333-8444-555555555555\","
                        "\"request_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
                        "\"response_status\":201,\"response_json\":\"{}\","
                        "\"expires_at\":\"2026-10-24T00:00:00Z\"}"));
    /* method 上限 8 字符、response_status 必须 200..599。 */
    TEST_CHECK(!validate("IdempotencyRecord",
                         "{\"actor_id\":\"11111111-2222-4333-8444-555555555555\",\"method\":\"POSTPOSTP\","
                         "\"path\":\"/operations\",\"key\":\"22222222-2222-4333-8444-555555555555\","
                         "\"request_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
                         "\"response_status\":201,\"response_json\":\"{}\","
                         "\"expires_at\":\"2026-10-24T00:00:00Z\"}"));
    TEST_CHECK(!validate("IdempotencyRecord",
                         "{\"actor_id\":\"11111111-2222-4333-8444-555555555555\",\"method\":\"POST\","
                         "\"path\":\"/operations\",\"key\":\"22222222-2222-4333-8444-555555555555\","
                         "\"request_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
                         "\"response_status\":199,\"response_json\":\"{}\","
                         "\"expires_at\":\"2026-10-24T00:00:00Z\"}"));
}

static void test_device_state(void)
{
    TEST_CHECK(validate("DeviceStateRecord",
                        "{\"device\":{\"device_id\":\"0123456789abcdef01234567\","
                        "\"cabinet_name\":\"locker-01\",\"contract_version\":\"0.2.1\","
                        "\"simulated\":false,\"mode\":\"READY\",\"health_reasons\":[],"
                        "\"ui_restart_count\":null,\"physical\":null},"
                        "\"last_snapshot_revision\":\"0\",\"active_command_id\":null}"));
    /* mode 必须是 4 个枚举之一。 */
    TEST_CHECK(!validate("DeviceStateRecord",
                         "{\"device\":{\"device_id\":\"0123456789abcdef01234567\","
                         "\"cabinet_name\":\"locker-01\",\"contract_version\":\"0.2.1\","
                         "\"simulated\":false,\"mode\":\"ONLINE\",\"health_reasons\":[],"
                         "\"ui_restart_count\":null,\"physical\":null},"
                         "\"last_snapshot_revision\":\"0\",\"active_command_id\":null}"));
}

int main(void)
{
    g_schema = locker_storage_schema_create();
    TEST_CHECK(g_schema != NULL);
    if (g_schema == NULL) {
        TEST_REPORT("storage.schema");
    }
    test_collections();
    test_user_record();
    test_session_and_card();
    test_tool_and_slot_guard();
    test_event_and_cursor();
    test_repository_change();
    test_query_dto();
    test_unknown_keyword_and_unknown_def();
    test_operation_record();
    test_outbox_and_idempotency();
    test_device_state();
    locker_storage_schema_destroy(g_schema);
    TEST_REPORT("storage.schema");
}
