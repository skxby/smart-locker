#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 单元测试：文档反序列化与公开投影（domain_state.c）、写手字节形状
 * （domain_docs.c）、状态机（domain_operation.c）、导入与设备状态落地
 * （domain_import.c）。
 *
 * 钉死的东西：
 *   1. U64 一律是十进制**字符串**，裸数字被拒；缺字段、错类型、非法枚举一律 fail closed。
 *   2. principal 复检同时看 session（属于本人、未撤销、未过期）与 user
 *      （enabled、role 一致），任一不符就是 INVALID，不是「当作没登录」。
 *   3. 投影的派生语义：工具可用性、device_mode 由相位推导、响应信封的 revision 文本。
 *   4. import_cursors 只由仓储写：Core 侧 domain_write_import_cursor 没有调用点，
 *      所以这里只断言「仓储已提交水位 -> ACK」，不声称 Core 自己写了导入游标
 *      （见 docs/evidence/stage-01/S1-04/handoff.md 的未实现项）。
 */

#include "domain_test_support.h"

#include <stdio.h>
#include <string.h>

/* ---------- 小工具 ---------- */

static bool st_parse_core(dt_fixture_t *fixture, locker_json_document_t *doc, const char *text)
{
    memset(doc, 0, sizeof(*doc));
    doc->nodes = fixture->core->arg_nodes;
    doc->node_capacity = LOCKER_DOMAIN_DOC_NODES;
    doc->members = fixture->core->arg_members;
    doc->member_capacity = LOCKER_DOMAIN_DOC_MEMBERS;
    return locker_domain_json_parse(doc, text, strlen(text));
}

/* 一个形状正确的 OperationRecord；调用方随后按需替换某个字段。 */
static void st_build_record(const locker_uuid_t *id, const locker_uuid_t *tool, const locker_uuid_t *actor,
                            const char *kind, const char *state, const char *revision, char *out,
                            size_t out_size)
{
    char id_text[LOCKER_DOMAIN_UUID_TEXT];
    char tool_text[LOCKER_DOMAIN_UUID_TEXT];
    char actor_text[LOCKER_DOMAIN_UUID_TEXT];
    locker_json_writer_t writer;

    dt_uuid_text(id, id_text, sizeof(id_text));
    dt_uuid_text(tool, tool_text, sizeof(tool_text));
    dt_uuid_text(actor, actor_text, sizeof(actor_text));

    locker_domain_json_writer_init(&writer, out, out_size);
    locker_domain_json_literal(&writer, "{\"operation\":{\"id\":");
    locker_domain_json_string(&writer, id_text);
    locker_domain_json_literal(&writer, ",\"tool_id\":");
    locker_domain_json_string(&writer, tool_text);
    locker_domain_json_literal(&writer, ",\"kind\":");
    locker_domain_json_string(&writer, kind);
    locker_domain_json_literal(&writer, ",\"actor_id\":");
    locker_domain_json_string(&writer, actor_text);
    locker_domain_json_literal(&writer, ",\"state\":");
    locker_domain_json_string(&writer, state);
    locker_domain_json_literal(&writer, ",\"operation_revision\":");
    locker_domain_json_string(&writer, revision);
    locker_domain_json_literal(&writer, ",\"command_id\":null,\"reason_code\":null,");
    locker_domain_json_literal(&writer, "\"created_at\":\"2026-10-26T00:00:00Z\",");
    locker_domain_json_literal(&writer, "\"updated_at\":\"2026-10-26T00:00:00Z\",");
    locker_domain_json_literal(&writer, "\"allowed_actions\":[],\"evidence\":{");
    locker_domain_json_literal(&writer, "\"history_complete\":true,\"door_open_seen\":false,");
    locker_domain_json_literal(&writer, "\"item_transition_seen\":false,\"physical\":null,");
    locker_domain_json_literal(&writer, "\"source_events\":[],\"manual_resolution\":false}},");
    locker_domain_json_literal(&writer, "\"baseline\":{\"device_id\":\"" DT_DEVICE_ID "\",");
    locker_domain_json_literal(&writer, "\"journal_epoch\":\"00000000-0000-4000-8000-000000000020\",");
    locker_domain_json_literal(&writer, "\"boot_id\":\"1\",\"physical_revision\":\"7\",");
    locker_domain_json_literal(&writer, "\"door\":\"CLOSED\",\"item\":\"PRESENT\",\"supply_mv\":12000,");
    locker_domain_json_literal(&writer, "\"observed_age_ms\":0,\"time_quality\":\"HOST_SYNCED\",");
    locker_domain_json_literal(&writer, "\"continuity_ok\":true,\"dropped_events\":\"0\",");
    locker_domain_json_literal(&writer, "\"durable_cursor\":{\"device_id\":\"" DT_DEVICE_ID "\",");
    locker_domain_json_literal(&writer, "\"journal_epoch\":\"00000000-0000-4000-8000-000000000020\",");
    locker_domain_json_literal(&writer, "\"boot_id\":\"1\",\"event_seq\":\"0\"}},");
    locker_domain_json_literal(&writer, "\"related_loan_id\":null,\"proof_source\":null,");
    locker_domain_json_literal(&writer, "\"proof_consumed\":false}");
}

/* ---------- 读取小工具 ---------- */

static void test_json_readers(void)
{
    dt_fixture_t fixture;
    locker_json_document_t doc;
    const locker_json_node_t *root;
    locker_uuid_t uuid;
    uint64_t u64 = 0u;
    int64_t integer = 0;
    bool boolean = true;
    uint8_t enumerated = 0u;
    char text[64];

    TEST_CHECK(dt_fixture_init(&fixture, false));
    TEST_CHECK(st_parse_core(&fixture, &doc,
                             "{\"u\":\"18446744073709551615\",\"n\":-12,\"b\":false,"
                             "\"s\":\"REVIEW_REQUIRED\",\"k\":\"BORROW\",\"d\":\"OPEN\",\"i\":\"ABSENT\","
                             "\"t\":\"HOST_SYNCED\",\"uuid\":\"0123abcd-0000-4000-8000-00000000000f\","
                             "\"label\":\"abcd\"}"));
    root = locker_domain_json_root(&doc);
    TEST_CHECK(root != NULL);

    TEST_CHECK(domain_json_read_u64(&doc, domain_json_member(&doc, root, "u"), &u64));
    TEST_CHECK_U64(UINT64_MAX, u64);
    TEST_CHECK(domain_json_read_int(&doc, domain_json_member(&doc, root, "n"), &integer));
    TEST_CHECK(integer == -12);
    TEST_CHECK(domain_json_read_bool(&doc, domain_json_member(&doc, root, "b"), &boolean));
    TEST_CHECK(!boolean);
    TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "label"), text, sizeof(text)));
    TEST_CHECK_STRING("abcd", text);
    TEST_CHECK(domain_json_read_uuid(&doc, domain_json_member(&doc, root, "uuid"), &uuid));
    TEST_CHECK(uuid.bytes[0] == 0x01 && uuid.bytes[15] == 0x0f);
    TEST_CHECK(domain_json_read_enum_state(&doc, domain_json_member(&doc, root, "s"), &enumerated));
    TEST_CHECK(enumerated == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK(domain_json_read_enum_kind(&doc, domain_json_member(&doc, root, "k"), &enumerated));
    TEST_CHECK(enumerated == LOCKER_OPERATION_KIND_BORROW);
    TEST_CHECK(domain_json_read_door(&doc, domain_json_member(&doc, root, "d"), &enumerated));
    TEST_CHECK(enumerated == LOCKER_DOOR_STATE_OPEN);
    TEST_CHECK(domain_json_read_item(&doc, domain_json_member(&doc, root, "i"), &enumerated));
    TEST_CHECK(enumerated == LOCKER_ITEM_STATE_ABSENT);
    TEST_CHECK(domain_json_read_time_quality(&doc, domain_json_member(&doc, root, "t"), &enumerated));
    TEST_CHECK(enumerated == LOCKER_TIME_QUALITY_HOST_SYNCED);

    /* 缺字段/错类型必须 false 且不改 out。 */
    u64 = 7u;
    TEST_CHECK(!domain_json_read_u64(&doc, NULL, &u64));
    TEST_CHECK_U64(7u, u64);
    TEST_CHECK(!domain_json_read_u64(&doc, domain_json_member(&doc, root, "label"), &u64));
    TEST_CHECK_U64(7u, u64);
    TEST_CHECK(!domain_json_read_bool(&doc, domain_json_member(&doc, root, "u"), &boolean));
    TEST_CHECK(!domain_json_read_door(&doc, domain_json_member(&doc, root, "u"), &enumerated));
    TEST_CHECK(!domain_json_read_item(&doc, domain_json_member(&doc, root, "s"), &enumerated));
    TEST_CHECK(!domain_json_read_time_quality(&doc, domain_json_member(&doc, root, "s"), &enumerated));
    TEST_CHECK(!domain_json_read_enum_kind(&doc, domain_json_member(&doc, root, "s"), &enumerated));
    TEST_CHECK(!domain_json_read_enum_state(&doc, domain_json_member(&doc, root, "label"), &enumerated));
    TEST_CHECK(!domain_json_read_uuid(&doc, domain_json_member(&doc, root, "label"), &uuid));

    /* U64 只接受规范十进制文本：裸数字、前导零、空串、超 20 位都拒。 */
    TEST_CHECK(st_parse_core(&fixture, &doc, "{\"u\":12}"));
    TEST_CHECK(!domain_json_read_u64(&doc, domain_json_member(&doc, locker_domain_json_root(&doc), "u"), &u64));
    TEST_CHECK(st_parse_core(&fixture, &doc, "{\"u\":\"012\"}"));
    TEST_CHECK(!domain_json_read_u64(&doc, domain_json_member(&doc, locker_domain_json_root(&doc), "u"), &u64));
    TEST_CHECK(st_parse_core(&fixture, &doc, "{\"u\":\"\"}"));
    TEST_CHECK(!domain_json_read_u64(&doc, domain_json_member(&doc, locker_domain_json_root(&doc), "u"), &u64));
    TEST_CHECK(st_parse_core(&fixture, &doc, "{\"u\":\"18446744073709551616\"}"));
    TEST_CHECK(!domain_json_read_u64(&doc, domain_json_member(&doc, locker_domain_json_root(&doc), "u"), &u64));

    /* device_mode 只认四个契约名，大小写敏感。 */
    TEST_CHECK(st_parse_core(&fixture, &doc, "{\"m\":\"RECONCILING\"}"));
    TEST_CHECK(domain_json_read_enum_device_mode(&doc, domain_json_member(&doc, locker_domain_json_root(&doc), "m"),
                                                 &enumerated));
    TEST_CHECK(enumerated == LOCKER_DEVICE_MODE_RECONCILING);
    TEST_CHECK(st_parse_core(&fixture, &doc, "{\"m\":\"ready\"}"));
    TEST_CHECK(!domain_json_read_enum_device_mode(&doc, domain_json_member(&doc, locker_domain_json_root(&doc), "m"),
                                                  &enumerated));
    TEST_CHECK(!domain_json_read_enum_device_mode(&doc, NULL, &enumerated));

    dt_fixture_free(&fixture);
}

/* ---------- OperationRecord 反序列化 ---------- */

static void test_operation_from_record(void)
{
    dt_fixture_t fixture;
    locker_json_document_t doc;
    const locker_json_node_t *root;
    domain_operation_t operation;
    locker_uuid_t id;
    locker_uuid_t tool;
    locker_uuid_t actor;
    char record[4096];

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_uuid_fill(&id, 0x11u);
    dt_uuid_fill(&tool, 0x22u);
    dt_uuid_fill(&actor, 0x33u);

    st_build_record(&id, &tool, &actor, "BORROW", "REQUESTED", "3", record, sizeof(record));
    TEST_CHECK(st_parse_core(&fixture, &doc, record));
    root = locker_domain_json_root(&doc);
    memset(&operation, 0, sizeof(operation));
    TEST_CHECK(domain_operation_from_record(&doc, root, &operation));
    TEST_CHECK(operation.used);
    TEST_CHECK(locker_uuid_equal(&operation.id, &id));
    TEST_CHECK(locker_uuid_equal(&operation.tool_id, &tool));
    TEST_CHECK(locker_uuid_equal(&operation.actor_id, &actor));
    TEST_CHECK(operation.kind == LOCKER_OPERATION_KIND_BORROW);
    TEST_CHECK(operation.state == LOCKER_OPERATION_STATE_REQUESTED);
    TEST_CHECK_U64(3u, operation.revision);
    TEST_CHECK(operation.has_baseline);
    TEST_CHECK(operation.baseline.valid);
    TEST_CHECK(operation.baseline.door == LOCKER_DOOR_STATE_CLOSED);
    TEST_CHECK(operation.baseline.item == LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK_U64(7u, operation.baseline.physical_revision);
    TEST_CHECK(operation.history_complete);
    TEST_CHECK(!operation.door_open_seen);
    TEST_CHECK(!operation.item_transition_seen);
    TEST_CHECK(!operation.has_physical);
    TEST_CHECK(operation.source_event_count == 0u);
    TEST_CHECK(!operation.has_loan);
    TEST_CHECK(operation.proof_source == 0u);
    TEST_CHECK(!operation.proof_consumed);
    TEST_CHECK(!operation.has_command);
    TEST_CHECK_STRING("", operation.reason_code);

    /* 缺 baseline -> 整条记录不可用（不能拿半个操作去推进状态）。 */
    {
        char bad[4096];
        const char *cut = "\"baseline\":";
        size_t prefix = 0u;
        size_t index = 0u;
        for (index = 0u; index + strlen(cut) <= strlen(record); index += 1u) {
            if (memcmp(record + index, cut, strlen(cut)) == 0) {
                prefix = index;
                break;
            }
        }
        TEST_CHECK(prefix > 0u);
        memcpy(bad, record, prefix);
        memcpy(bad + prefix, "\"related_loan_id\":null,\"proof_source\":null,\"proof_consumed\":false}",
               strlen("\"related_loan_id\":null,\"proof_source\":null,\"proof_consumed\":false}") + 1u);
        TEST_CHECK(st_parse_core(&fixture, &doc, bad));
        memset(&operation, 0, sizeof(operation));
        TEST_CHECK(!domain_operation_from_record(&doc, locker_domain_json_root(&doc), &operation));
    }

    /* 未知 state/kind、裸数字 revision、非 uuid 字段一律 fail closed。 */
    st_build_record(&id, &tool, &actor, "BORROW", "AROUND_HALF", "3", record, sizeof(record));
    TEST_CHECK(st_parse_core(&fixture, &doc, record));
    memset(&operation, 0, sizeof(operation));
    TEST_CHECK(!domain_operation_from_record(&doc, locker_domain_json_root(&doc), &operation));
    st_build_record(&id, &tool, &actor, "TRANSFER", "REQUESTED", "3", record, sizeof(record));
    TEST_CHECK(st_parse_core(&fixture, &doc, record));
    TEST_CHECK(!domain_operation_from_record(&doc, locker_domain_json_root(&doc), &operation));
    st_build_record(&id, &tool, &actor, "BORROW", "REQUESTED", "003", record, sizeof(record));
    TEST_CHECK(st_parse_core(&fixture, &doc, record));
    TEST_CHECK(!domain_operation_from_record(&doc, locker_domain_json_root(&doc), &operation));
    TEST_CHECK(!domain_operation_from_record(NULL, NULL, &operation));

    /* proof_source 只接受 null/CARD/LOCAL_UI。 */
    {
        static const char *const k_proofs[] = {"CARD", "LOCAL_UI"};
        static const uint8_t k_expected[] = {1u, 2u};
        uint32_t i;
        for (i = 0u; i < 2u; ++i) {
            st_build_record(&id, &tool, &actor, "BORROW", "WAIT_CONFIRM", "5", record, sizeof(record));
            {
                char patched[4096];
                const char *marker = "\"proof_source\":null";
                size_t offset = 0u;
                size_t index = 0u;
                for (index = 0u; index + strlen(marker) <= strlen(record); index += 1u) {
                    if (memcmp(record + index, marker, strlen(marker)) == 0) {
                        offset = index;
                        break;
                    }
                }
                TEST_CHECK(offset > 0u);
                memcpy(patched, record, offset);
                memcpy(patched + offset, "\"proof_source\":\"", strlen("\"proof_source\":\""));
                memcpy(patched + offset + strlen("\"proof_source\":\""), k_proofs[i], strlen(k_proofs[i]));
                memcpy(patched + offset + strlen("\"proof_source\":\"") + strlen(k_proofs[i]), "\"",
                       strlen("\""));
                memcpy(patched + offset + strlen("\"proof_source\":\"") + strlen(k_proofs[i]) + strlen("\""),
                       record + offset + strlen(marker), strlen(record + offset + strlen(marker)) + 1u);
                TEST_CHECK(st_parse_core(&fixture, &doc, patched));
                memset(&operation, 0, sizeof(operation));
                TEST_CHECK(domain_operation_from_record(&doc, locker_domain_json_root(&doc), &operation));
                TEST_CHECK(operation.proof_source == k_expected[i]);
            }
        }
        st_build_record(&id, &tool, &actor, "BORROW", "WAIT_CONFIRM", "5", record, sizeof(record));
        {
            char patched[4096];
            const char *marker = "\"proof_source\":null";
            size_t offset = 0u;
            size_t index = 0u;
            for (index = 0u; index + strlen(marker) <= strlen(record); index += 1u) {
                if (memcmp(record + index, marker, strlen(marker)) == 0) {
                    offset = index;
                    break;
                }
            }
            memcpy(patched, record, offset);
            memcpy(patched + offset, "\"proof_source\":\"CARD_X\"",
                   strlen("\"proof_source\":\"CARD_X\""));
            memcpy(patched + offset + strlen("\"proof_source\":\"CARD_X\""), record + offset + strlen(marker),
                   strlen(record + offset + strlen(marker)) + 1u);
            TEST_CHECK(st_parse_core(&fixture, &doc, patched));
            memset(&operation, 0, sizeof(operation));
            TEST_CHECK(!domain_operation_from_record(&doc, locker_domain_json_root(&doc), &operation));
        }
    }

    dt_fixture_free(&fixture);
}

/* ---------- 从仓储加载（deadline 重建） ---------- */

static void test_operation_load(void)
{
    dt_fixture_t fixture;
    domain_operation_t operation;
    locker_uuid_t id;
    locker_uuid_t missing;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&missing, 0x61u);

    /* updated_at 是 2026-10-26T00:00:00Z，REQUESTED 的截止点 = 更新时间 + 120 秒。 */
    TEST_CHECK(dt_seed_operation(&fixture, &id, &id, &id, LOCKER_OPERATION_KIND_BORROW,
                                 LOCKER_OPERATION_STATE_REQUESTED, 4u, "2026-10-26T00:00:00Z"));
    memset(&operation, 0, sizeof(operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 0, &operation));
    TEST_CHECK_U64(4u, operation.revision);
    TEST_CHECK(operation.state == LOCKER_OPERATION_STATE_REQUESTED);
    TEST_CHECK(operation.requested_deadline_ms != 0u);
    {
        uint64_t wall = 0u;
        TEST_CHECK(locker_domain_time_parse_utc("2026-10-26T00:00:00Z", 20u, &wall));
        TEST_CHECK_U64(wall + LOCKER_REQUEST_TTL_MS, operation.requested_deadline_ms);
        TEST_CHECK_U64(0u, operation.wait_confirm_deadline_ms);
    }

    /* 缺记录是 NOT_READY（不是 IO_ERROR：没找到不等于读坏了）。 */
    memset(&operation, 0, sizeof(operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_operation_load(fixture.core, &missing, 0, &operation));

    /* 槽位越界/空指针是 INVALID。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_operation_load(fixture.core, &id, 2, &operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_operation_load(fixture.core, &id, -1, &operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_operation_load(fixture.core, NULL, 0, &operation));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_operation_load(fixture.core, &id, 0, NULL));

    /* 仓储错误原样上抛。 */
    fixture.repo->query_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, domain_operation_load(fixture.core, &id, 0, &operation));
    fixture.repo->query_status = LOCKER_STATUS_OK;

    /* 记录存在但内容畸形 -> IO_ERROR（不能当成缺失，更不能当成可用）。 */
    {
        char key[LOCKER_DOMAIN_UUID_TEXT];
        dt_uuid_text(&id, key, sizeof(key));
        TEST_CHECK(dt_repo_put(fixture.repo, "operations", key, "{\"operation\":{}}"));
        TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, domain_operation_load(fixture.core, &id, 0, &operation));
    }

    dt_fixture_free(&fixture);
}

/* ---------- principal 复检 ---------- */

static void st_override_text(dt_fixture_t *fixture, const char *collection, const char *key,
                             const char *expected, const char *replacement)
{
    dt_doc_t *doc = dt_repo_find(fixture->repo, collection, key);
    char buffer[DT_VALUE_BYTES];
    char *hit;
    TEST_CHECK(doc != NULL);
    if (doc == NULL) {
        return;
    }
    hit = strstr(doc->value, expected);
    TEST_CHECK(hit != NULL);
    if (hit == NULL) {
        return;
    }
    {
        size_t prefix = (size_t)(hit - doc->value);
        memcpy(buffer, doc->value, prefix);
        memcpy(buffer + prefix, replacement, strlen(replacement));
        memcpy(buffer + prefix + strlen(replacement), doc->value + prefix + strlen(expected),
               strlen(doc->value + prefix + strlen(expected)) + 1u);
    }
    TEST_CHECK(dt_repo_put(fixture->repo, collection, key, buffer));
}

static void test_principal_check(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_principal_t broken;
    locker_uuid_t tool;
    char session_key[LOCKER_DOMAIN_UUID_TEXT];

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    dt_uuid_fill(&tool, 0x22u);

    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_principal_check(fixture.core, &principal));

    /* 未认证 / nil id / 未知角色 一律 INVALID。 */
    broken = principal;
    broken.authenticated = false;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &broken));
    broken = principal;
    locker_uuid_clear(&broken.session_id);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &broken));
    broken = principal;
    locker_uuid_clear(&broken.user_id);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &broken));
    broken = principal;
    broken.role = 7u;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &broken));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(NULL, &principal));

    /* 会话不属于本人 -> INVALID。 */
    broken = dt_principal(LOCKER_ROLE_USER, 0x72u);
    broken.session_id = principal.session_id;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &broken));

    /* 会话被撤销 -> INVALID。 */
    dt_uuid_text(&principal.session_id, session_key, sizeof(session_key));
    st_override_text(&fixture, "sessions", session_key, "\"revoked\":false", "\"revoked\":true");
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &principal));
    st_override_text(&fixture, "sessions", session_key, "\"revoked\":true", "\"revoked\":false");
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_principal_check(fixture.core, &principal));

    /* 会话已过期 -> INVALID（墙钟来自注入时钟，不是系统时间）。 */
    {
        /* dt_seed_principal 写的是 wall + 1h，所以定位 marker 必须用同一个值。 */
        char seeded[LOCKER_DOMAIN_TIMESTAMP_TEXT];
        char marker[LOCKER_DOMAIN_TIMESTAMP_TEXT + 16];
        TEST_CHECK(locker_domain_time_format_utc(fixture.clock.wall_ms + 3600000ull, seeded,
                                                sizeof(seeded)));
        memcpy(marker, "\"expires_at\":\"", sizeof("\"expires_at\":\""));
        memcpy(marker + strlen("\"expires_at\":\""), seeded, strlen(seeded) + 1u);
        memcpy(marker + strlen("\"expires_at\":\"") + strlen(seeded), "\"", strlen("\"") + 1u);
        st_override_text(&fixture, "sessions", session_key, marker,
                         "\"expires_at\":\"2020-01-01T00:00:00Z\"");
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &principal));
        st_override_text(&fixture, "sessions", session_key, "\"expires_at\":\"2020-01-01T00:00:00Z\"",
                         marker);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_principal_check(fixture.core, &principal));
    }

    /* 用户被禁用 -> INVALID。 */
    {
        char user_key[LOCKER_DOMAIN_UUID_TEXT];
        dt_uuid_text(&principal.user_id, user_key, sizeof(user_key));
        st_override_text(&fixture, "users", user_key, "\"enabled\":true", "\"enabled\":false");
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &principal));
        st_override_text(&fixture, "users", user_key, "\"enabled\":false", "\"enabled\":true");
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_principal_check(fixture.core, &principal));

        /* 角色不一致 -> INVALID：不能拿 USER 的会话当 ADMIN 用。 */
        broken = principal;
        broken.role = LOCKER_ROLE_ADMIN;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_principal_check(fixture.core, &broken));
    }

    /* 墙钟不可用 -> NOT_READY（不是 INVALID：无法判断过期就不能放行）。 */
    fixture.clock.wall_ms = 0u;
    {
        test_clock_t saved = fixture.clock;
        fixture.clock.wall_ms = 0u;
        /* wall 回调返回 true 但值为 0，仍然能解析成 1970；要真正失败得换 port。 */
        fixture.core->clock.wall_utc_ms = NULL;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_principal_check(fixture.core, &principal));
        fixture.core->clock.wall_utc_ms = dt_clock_wall;
        fixture.clock = saved;
        fixture.clock.wall_ms = 1761004800000ull;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_principal_check(fixture.core, &principal));

    (void)tool;
    dt_fixture_free(&fixture);
}

/* ---------- 投影 ---------- */

static void test_projections(void)
{
    dt_fixture_t fixture;
    locker_json_writer_t writer;
    locker_json_document_t doc;
    const locker_json_node_t *root;
    char buffer[4096];
    uint8_t availability;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* 相位 -> DeviceMode：READY/RECOVERING/其它。 */
    fixture.core->phase = DOMAIN_PHASE_READY;
    TEST_CHECK(domain_core_device_mode(fixture.core) == LOCKER_DEVICE_MODE_READY);
    fixture.core->phase = DOMAIN_PHASE_RECOVERING;
    TEST_CHECK(domain_core_device_mode(fixture.core) == LOCKER_DEVICE_MODE_RECONCILING);
    fixture.core->phase = DOMAIN_PHASE_INIT;
    TEST_CHECK(domain_core_device_mode(fixture.core) == LOCKER_DEVICE_MODE_OFFLINE);
    fixture.core->phase = DOMAIN_PHASE_STOPPED;
    TEST_CHECK(domain_core_device_mode(fixture.core) == LOCKER_DEVICE_MODE_OFFLINE);

    /* 工具可用性：非 READY -> UNAVAILABLE；REVIEW -> REVIEW；占位 -> RESERVED；
       有效借用 -> BORROWED；否则 AVAILABLE。终态不占位。 */
    availability = domain_tool_availability(fixture.core);
    TEST_CHECK(availability == LOCKER_TOOL_AVAILABILITY_UNAVAILABLE);
    fixture.core->phase = DOMAIN_PHASE_READY;
    TEST_CHECK(domain_tool_availability(fixture.core) == LOCKER_TOOL_AVAILABILITY_AVAILABLE);
    fixture.core->loan_open = true;
    TEST_CHECK(domain_tool_availability(fixture.core) == LOCKER_TOOL_AVAILABILITY_BORROWED);
    fixture.core->loan_open = false;
    fixture.core->active.used = true;
    fixture.core->active.state = LOCKER_OPERATION_STATE_ACCESS_PENDING;
    TEST_CHECK(domain_tool_availability(fixture.core) == LOCKER_TOOL_AVAILABILITY_RESERVED);
    fixture.core->active.state = LOCKER_OPERATION_STATE_REVIEW_REQUIRED;
    TEST_CHECK(domain_tool_availability(fixture.core) == LOCKER_TOOL_AVAILABILITY_REVIEW);
    fixture.core->active.state = LOCKER_OPERATION_STATE_COMPLETED;
    TEST_CHECK(domain_tool_availability(fixture.core) == LOCKER_TOOL_AVAILABILITY_AVAILABLE);
    fixture.core->active.state = LOCKER_OPERATION_STATE_FAILED;
    TEST_CHECK(domain_tool_availability(fixture.core) == LOCKER_TOOL_AVAILABILITY_AVAILABLE);
    fixture.core->active.used = false;

    /* 无工具投影时写 null（不是空对象）。 */
    writer.buffer = buffer;
    writer.capacity = sizeof(buffer);
    writer.length = 0u;
    writer.failed = false;
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_tool_projection(&writer, fixture.core);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK_STRING("null", buffer);

    /* 设备投影：无快照时 device_id 全零、physical null、mode 由相位决定。 */
    fixture.core->phase = DOMAIN_PHASE_RECOVERING;
    fixture.core->device_snapshot_valid = false;
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_device_projection(&writer, fixture.core);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_parse_core(&fixture, &doc, buffer));
    root = locker_domain_json_root(&doc);
    {
        char text[128];
        TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "device_id"), text, sizeof(text)));
        TEST_CHECK_STRING("000000000000000000000000", text);
        TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "mode"), text, sizeof(text)));
        TEST_CHECK_STRING("RECONCILING", text);
        TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "cabinet_name"), text, sizeof(text)));
        TEST_CHECK_STRING("test-cabinet", text);
        TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "contract_version"), text,
                                         sizeof(text)));
        TEST_CHECK_STRING(LOCKER_CONTRACT_VERSION, text);
        TEST_CHECK(domain_json_member(&doc, root, "physical") != NULL);
        TEST_CHECK(domain_json_member(&doc, root, "physical")->type == LOCKER_JSON_NULL);
        TEST_CHECK(domain_json_member(&doc, root, "ui_restart_count")->type == LOCKER_JSON_NULL);
        TEST_CHECK(domain_json_member(&doc, root, "health_reasons")->type == LOCKER_JSON_ARRAY);
        TEST_CHECK(domain_json_member(&doc, root, "health_reasons")->count == 0u);
    }

    /* 有快照时写真实 device_id 与 physical，mode 跟随相位（READY）。 */
    dt_core_device_ready(&fixture, 3u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_device_projection(&writer, fixture.core);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_parse_core(&fixture, &doc, buffer));
    root = locker_domain_json_root(&doc);
    {
        char text[128];
        TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "device_id"), text, sizeof(text)));
        TEST_CHECK_STRING(DT_DEVICE_ID, text);
        TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "mode"), text, sizeof(text)));
        TEST_CHECK_STRING("READY", text);
        TEST_CHECK(domain_json_member(&doc, root, "physical")->type == LOCKER_JSON_OBJECT);
    }

    dt_fixture_free(&fixture);
}

/* ---------- 响应信封 ---------- */

static void test_response_envelope(void)
{
    dt_fixture_t fixture;
    locker_json_writer_t writer;
    locker_json_document_t doc;
    const locker_json_node_t *root;
    locker_uuid_t request_id;
    char buffer[1024];
    char text[128];
    size_t length = 0u;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    dt_uuid_fill(&request_id, 0x99u);

    domain_response_begin(&writer, buffer, sizeof(buffer));
    TEST_CHECK(domain_response_data_object(&writer));
    locker_domain_json_literal(&writer, "\"ok\":");
    locker_domain_json_bool(&writer, true);
    domain_response_end(&writer, UINT64_MAX, &request_id);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_parse_core(&fixture, &doc, buffer));
    root = locker_domain_json_root(&doc);
    TEST_CHECK(root->type == LOCKER_JSON_OBJECT);
    TEST_CHECK(domain_json_member(&doc, root, "data") != NULL);
    TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "snapshot_revision"), text, sizeof(text)));
    TEST_CHECK_STRING("18446744073709551615", text);
    TEST_CHECK(locker_domain_json_text(&doc, domain_json_member(&doc, root, "request_id"), text, sizeof(text),
                                       &length));
    TEST_CHECK(strcmp(text, "8899aabb-ccdd-4eef-8099-8899aabbcc0b") != 0); /* 只断言不是全零 */
    TEST_CHECK(strcmp(text, "00000000-0000-0000-0000-000000000000") != 0);

    /* request_id 为 NULL 时写全零 UUID，而不是省略字段。 */
    domain_response_begin(&writer, buffer, sizeof(buffer));
    TEST_CHECK(domain_response_data_object(&writer));
    domain_response_end(&writer, 7u, NULL);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_parse_core(&fixture, &doc, buffer));
    root = locker_domain_json_root(&doc);
    TEST_CHECK(locker_domain_json_text(&doc, domain_json_member(&doc, root, "request_id"), text, sizeof(text),
                                       &length));
    TEST_CHECK_STRING("00000000-0000-0000-0000-000000000000", text);
    TEST_CHECK(domain_json_read_text(&doc, domain_json_member(&doc, root, "snapshot_revision"), text, sizeof(text)));
    TEST_CHECK_STRING("7", text);

    /* writer 已失败时 response_end 不再写任何字节。 */
    locker_domain_json_writer_init(&writer, buffer, 4u);
    domain_response_begin(&writer, buffer, 4u);
    domain_response_end(&writer, 1u, NULL);
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));

    dt_fixture_free(&fixture);
}

/* ---------- 写手字节形状 ---------- */

static bool st_has_pair(const char *text, const char *key, const char *value)
{
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\":%s", key, value);
    return strstr(text, pattern) != NULL;
}

static void test_writers(void)
{
    dt_fixture_t fixture;
    locker_json_writer_t writer;
    char buffer[8192];
    locker_uuid_t id;
    locker_uuid_t other;
    locker_event_key_t key;
    domain_physical_t physical;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    dt_uuid_fill(&id, 0x31u);
    dt_uuid_fill(&other, 0x41u);

    memset(&key, 0, sizeof(key));
    memcpy(key.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    key.journal_epoch = id;
    key.boot_id = 9u;
    key.event_seq = 12u;

    /* 事件键：U64 是十进制字符串，device_id 是 24 位 hex 文本。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_event_key(&writer, &key);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(strstr(buffer, "\"boot_id\":\"9\"") != NULL);
    TEST_CHECK(strstr(buffer, "\"event_seq\":\"12\"") != NULL);
    TEST_CHECK(strstr(buffer, "\"device_id\":\"" DT_DEVICE_ID "\"") != NULL);

    /* 物理快照：supply_mv 有效时写裸数字，observed_age_ms 恒 0。 */
    memset(&physical, 0, sizeof(physical));
    physical.valid = true;
    memcpy(physical.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
    physical.journal_epoch = id;
    physical.boot_id = 9u;
    physical.physical_revision = 44u;
    physical.door = LOCKER_DOOR_STATE_OPEN;
    physical.item = LOCKER_ITEM_STATE_ABSENT;
    physical.supply_valid = true;
    physical.supply_mv = 12345u;
    physical.time_quality = LOCKER_TIME_QUALITY_ESTIMATED;
    physical.continuity_ok = false;
    physical.dropped_events = 3u;
    physical.durable_cursor = key;
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_physical(&writer, &physical);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "physical_revision", "\"44\""));
    TEST_CHECK(st_has_pair(buffer, "door", "\"OPEN\""));
    TEST_CHECK(st_has_pair(buffer, "item", "\"ABSENT\""));
    TEST_CHECK(st_has_pair(buffer, "supply_mv", "12345"));
    TEST_CHECK(st_has_pair(buffer, "observed_age_ms", "0"));
    TEST_CHECK(st_has_pair(buffer, "time_quality", "\"ESTIMATED\""));
    TEST_CHECK(st_has_pair(buffer, "continuity_ok", "false"));
    TEST_CHECK(st_has_pair(buffer, "dropped_events", "\"3\""));

    /* 供电压未知：supply_mv 必须是 null，不能写 0 冒充测量值。 */
    physical.supply_valid = false;
    physical.supply_mv = 0u;
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_physical(&writer, &physical);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "supply_mv", "null"));

    /* OperationRecord：没有 baseline 时写手必须失败，不能产出半条记录。 */
    {
        domain_operation_t operation;
        memset(&operation, 0, sizeof(operation));
        operation.used = true;
        operation.id = id;
        operation.tool_id = other;
        operation.actor_id = id;
        operation.kind = LOCKER_OPERATION_KIND_RETURN;
        operation.state = LOCKER_OPERATION_STATE_REQUESTED;
        operation.revision = 1u;
        memcpy(operation.created_at, "2026-10-26T01:02:03Z", sizeof("2026-10-26T01:02:03Z"));
        memcpy(operation.updated_at, "2026-10-26T01:02:03Z", sizeof("2026-10-26T01:02:03Z"));

        locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
        domain_write_operation_record(&writer, &operation, &fixture.core->instance_id);
        TEST_CHECK(!locker_domain_json_writer_ok(&writer));

        /* host_instance_id 必须是有效 UUID。 */
        operation.has_baseline = true;
        operation.baseline = physical;
        locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
        domain_write_operation_record(&writer, &operation, NULL);
        TEST_CHECK(!locker_domain_json_writer_ok(&writer));

        locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
        domain_write_operation_record(&writer, &operation, &fixture.core->instance_id);
        TEST_CHECK(locker_domain_json_writer_ok(&writer));
        TEST_CHECK(st_has_pair(buffer, "kind", "\"RETURN\""));
        TEST_CHECK(st_has_pair(buffer, "state", "\"REQUESTED\""));
        TEST_CHECK(st_has_pair(buffer, "operation_revision", "\"1\""));
        TEST_CHECK(st_has_pair(buffer, "related_loan_id", "null"));
        TEST_CHECK(st_has_pair(buffer, "proof_consumed", "false"));
        TEST_CHECK(st_has_pair(buffer, "host_instance_id", "\"40414243-4445-4647-8849-4a4b4c4d4e4f\""));
    }

    /* 幂等记录：response_status 是裸数字，response_json 为 null 时退化 "{}"。 */
    {
        locker_uuid_t key_id;
        dt_uuid_fill(&key_id, 0x51u);
        locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
        domain_write_idempotency(&writer, &id, "POST", LOCKER_CORE_OPERATION_CREATE, &key_id,
                                 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 201, NULL,
                                 "2026-11-25T00:00:00Z");
        TEST_CHECK(locker_domain_json_writer_ok(&writer));
        TEST_CHECK(st_has_pair(buffer, "response_status", "201"));
        TEST_CHECK(st_has_pair(buffer, "response_json", "\"{}\""));
        TEST_CHECK(st_has_pair(buffer, "method", "\"POST\""));
    }

    /* Audit：reason 为空时写 "-"，type 缺省是 EVENT。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_audit(&writer, &id, &id, &other, NULL, NULL, NULL, false, NULL, LOCKER_TIME_QUALITY_UNSYNCED);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "type", "\"EVENT\""));
    TEST_CHECK(st_has_pair(buffer, "reason", "\"-\""));
    TEST_CHECK(st_has_pair(buffer, "occurred_at", "null"));
    TEST_CHECK(st_has_pair(buffer, "source_event", "null"));

    /* 槽位守卫固定 slot_id=1（单柜格）。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_slot_guard(&writer, &id);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "slot_id", "1"));

    /* 导入游标：continuity_ok 是 bool，through 是完整事件键。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_import_cursor(&writer, &id, DT_DEVICE_ID, &key, false);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "continuity_ok", "false"));
    TEST_CHECK(st_has_pair(buffer, "device_id", "\"" DT_DEVICE_ID "\""));

    /* device_state：simulated 只能是 false（契约缺口 CR-S104-1）。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_device_state(&writer, "cab-1", LOCKER_CONTRACT_VERSION, false, LOCKER_DEVICE_MODE_READY,
                              &physical, 5u, NULL, false);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "simulated", "false"));
    TEST_CHECK(st_has_pair(buffer, "last_snapshot_revision", "\"5\""));
    TEST_CHECK(st_has_pair(buffer, "active_command_id", "null"));
    TEST_CHECK(st_has_pair(buffer, "cabinet_name", "\"cab-1\""));
    TEST_CHECK(strstr(buffer, "\"contract_version\":\"" LOCKER_CONTRACT_VERSION "\"") != NULL);

    /* Loan：未归还时 closed_at 是 null，不写空串。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    domain_write_loan(&writer, &id, &other, &other, &id, NULL, false, "2026-10-26T00:00:00Z", NULL, false);
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK(st_has_pair(buffer, "return_operation_id", "null"));
    TEST_CHECK(st_has_pair(buffer, "closed_at", "null"));
    TEST_CHECK(st_has_pair(buffer, "borrow_operation_id", "\"31323334-3536-4738-b93a-3b3c3d3e3f40\""));

    dt_fixture_free(&fixture);
}

/* ---------- 状态机 ---------- */

static void test_operation_state_machine(void)
{
    dt_fixture_t fixture;
    domain_operation_t operation;
    uint8_t actions[8];
    uint32_t count = 0u;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);
    memset(&operation, 0, sizeof(operation));

    /* 允许动作表：REQUESTED 可到柜/等待/取消，WAIT_CONFIRM 只能确认/等待，
       REVIEW 只能管理员核验，终态为空。 */
    operation.used = true;
    operation.state = LOCKER_OPERATION_STATE_REQUESTED;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 3u);
    TEST_CHECK(actions[0] == LOCKER_ALLOWED_ACTION_PRESENT_AT_CABINET);
    TEST_CHECK(actions[1] == LOCKER_ALLOWED_ACTION_WAIT);
    TEST_CHECK(actions[2] == LOCKER_ALLOWED_ACTION_CANCEL);
    operation.state = LOCKER_OPERATION_STATE_ACCESS_PENDING;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 1u);
    TEST_CHECK(actions[0] == LOCKER_ALLOWED_ACTION_WAIT);
    operation.state = LOCKER_OPERATION_STATE_ACCESS_ACTIVE;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 1u);
    operation.state = LOCKER_OPERATION_STATE_WAIT_CONFIRM;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 2u);
    TEST_CHECK(actions[0] == LOCKER_ALLOWED_ACTION_CONFIRM);
    TEST_CHECK(actions[1] == LOCKER_ALLOWED_ACTION_WAIT);
    operation.state = LOCKER_OPERATION_STATE_REVIEW_REQUIRED;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 1u);
    TEST_CHECK(actions[0] == LOCKER_ALLOWED_ACTION_ADMIN_REVIEW);
    operation.state = LOCKER_OPERATION_STATE_COMPLETED;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 0u);
    operation.state = LOCKER_OPERATION_STATE_NO_CHANGE;
    domain_operation_allowed_actions(&operation, actions, &count);
    TEST_CHECK(count == 0u);
    domain_operation_allowed_actions(NULL, actions, &count);
    TEST_CHECK(count == 0u);

    /* 转移只允许单调前进：同状态、回退、从终态出发都 CONFLICT。 */
    operation.state = LOCKER_OPERATION_STATE_REQUESTED;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_REQUESTED, "SAME"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_ACCESS_PENDING,
                                                 "GO"));
    TEST_CHECK(operation.state == LOCKER_OPERATION_STATE_ACCESS_PENDING);
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_REQUESTED, "BACK"));
    TEST_CHECK(operation.state == LOCKER_OPERATION_STATE_ACCESS_PENDING);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_operation_set_state(fixture.core, &operation, 42u, "BOGUS"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_COMPLETED, "DONE"));
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                                 "AFTER"));
    operation.used = false;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_ACCESS_PENDING, "X"));

    /* WAIT_CONFIRM 设 120 秒确认期限，REVIEW_REQUIRED 清掉它。 */
    memset(&operation, 0, sizeof(operation));
    operation.used = true;
    operation.state = LOCKER_OPERATION_STATE_ACCESS_ACTIVE;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_WAIT_CONFIRM,
                                                 "PHYSICAL_CONFIRMED"));
    TEST_CHECK_U64(fixture.core->now_ms + LOCKER_WAIT_CONFIRM_MS, operation.wait_confirm_deadline_ms);
    TEST_CHECK_STRING("PHYSICAL_CONFIRMED", operation.reason_code);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      domain_operation_set_state(fixture.core, &operation, LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
                                                 "ADMIN"));
    TEST_CHECK_U64(0u, operation.wait_confirm_deadline_ms);

    /* touch 每次推进 revision 并刷新 updated_at；created_at 只在第一次写。 */
    memset(&operation, 0, sizeof(operation));
    operation.used = true;
    fixture.clock.wall_ms = 1761004800000ull;
    domain_operation_touch(fixture.core, &operation);
    TEST_CHECK_U64(1u, operation.revision);
    TEST_CHECK_STRING("2025-10-21T00:00:00Z", operation.created_at);
    TEST_CHECK_STRING("2025-10-21T00:00:00Z", operation.updated_at);
    fixture.clock.wall_ms += 2000u;
    domain_operation_touch(fixture.core, &operation);
    TEST_CHECK_U64(2u, operation.revision);
    TEST_CHECK_STRING("2025-10-21T00:00:00Z", operation.created_at);
    TEST_CHECK_STRING("2025-10-21T00:00:02Z", operation.updated_at);

    /* 来源事件按四元组去重，超 32 条置 history_complete=false。 */
    {
        locker_event_key_t first;
        locker_event_key_t second;
        uint32_t index;
        memset(&operation, 0, sizeof(operation));
        operation.used = true;
        operation.history_complete = true;
        memset(&first, 0, sizeof(first));
        memcpy(first.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));
        dt_uuid_fill(&first.journal_epoch, 0x20u);
        first.boot_id = 1u;
        first.event_seq = 5u;
        second = first;
        second.event_seq = 6u;
        domain_operation_add_source_event(&operation, &first);
        domain_operation_add_source_event(&operation, &first);
        TEST_CHECK(operation.source_event_count == 1u);
        domain_operation_add_source_event(&operation, &second);
        TEST_CHECK(operation.source_event_count == 2u);
        for (index = 0u; index < LOCKER_DOMAIN_SOURCE_EVENTS; ++index) {
            locker_event_key_t extra = second;
            extra.event_seq = 100u + index;
            domain_operation_add_source_event(&operation, &extra);
        }
        TEST_CHECK(operation.source_event_count == LOCKER_DOMAIN_SOURCE_EVENTS);
        TEST_CHECK(!operation.history_complete);
    }

    dt_fixture_free(&fixture);
}

/* ---------- 导入与设备状态落地 ---------- */

static void st_fill_page(locker_wire_fetch_events_response_t *page, uint8_t count, uint64_t first_seq,
                         bool gap)
{
    uint8_t index;
    memset(page, 0, sizeof(*page));
    dt_uuid_fill(&page->journal_epoch, 0x20u);
    page->event_count = count;
    page->gap_before_page = gap ? 1u : 0u;
    page->first_available.boot_id = 3u;
    page->first_available.event_seq = first_seq;
    page->last_committed.boot_id = 3u;
    page->last_committed.event_seq = first_seq + count - 1u;
    page->next_after = page->last_committed;
    /* events[] 只有 LOCKER_UART_MAX_EVENTS 项。这里允许 count 故意超上限，
       供「非法页被拒」的用例使用，但填充必须停在数组容量内，否则是本测试
       自己写越界（ASan 实测 stack-buffer-overflow），与被测代码无关。 */
    uint8_t fill = count > LOCKER_UART_MAX_EVENTS ? (uint8_t)LOCKER_UART_MAX_EVENTS : count;
    for (index = 0u; index < fill; ++index) {
        page->events[index].boot_id = 3u;
        page->events[index].event_seq = first_seq + index;
        page->events[index].event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_CLOSED;
        page->events[index].door = LOCKER_DOOR_STATE_CLOSED;
        page->events[index].item = LOCKER_ITEM_STATE_PRESENT;
        page->events[index].physical_revision = 7u;
    }
}

static void test_import_page(void)
{
    dt_fixture_t fixture;
    locker_wire_fetch_events_response_t page;
    locker_event_key_t committed;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_device_ready(&fixture, 3u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);

    /* 空页不推进水位、也不调仓储。 */
    st_fill_page(&page, 0u, 1u, false);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_import_page(fixture.core, &page));
    TEST_CHECK(fixture.repo->import_calls == 0u);
    TEST_CHECK(!fixture.core->import_cursor_valid);

    /* 身份未知时不落库、不猜占位 device_id。 */
    fixture.core->cursor.device_id[0] = '\0';
    st_fill_page(&page, 2u, 1u, false);
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_import_page(fixture.core, &page));
    TEST_CHECK(fixture.repo->import_calls == 0u);
    memcpy(fixture.core->cursor.device_id, DT_DEVICE_ID, sizeof(DT_DEVICE_ID));

    /* 超过 max_event_count 的页是非法输入。 */
    st_fill_page(&page, (uint8_t)(LOCKER_DOMAIN_EVENT_PAGE_MAX + 1u), 1u, false);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, domain_import_page(fixture.core, &page));

    /* 首帧连续：import_continuity=true，ACK 跟到仓储提交的水位。 */
    st_fill_page(&page, 3u, 10u, false);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_import_page(fixture.core, &page));
    TEST_CHECK(fixture.repo->import_calls == 1u);
    TEST_CHECK(fixture.repo->last_import_count == 3u);
    TEST_CHECK(!fixture.repo->last_import_has_projection); /* 投影必须传 NULL */
    TEST_CHECK(!fixture.repo->last_import_gap);
    TEST_CHECK(fixture.core->import_cursor_valid);
    TEST_CHECK(fixture.core->import_continuity);
    TEST_CHECK_U64(12u, fixture.core->import_cursor.event_seq);
    TEST_CHECK(fixture.core->ack_cursor_valid);
    TEST_CHECK_U64(12u, fixture.core->ack_cursor.event_seq);
    TEST_CHECK_STRING(DT_DEVICE_ID, fixture.core->import_cursor.device_id);

    /* 后续页带 gap：连续性锁死为 false，之后不再回真。 */
    st_fill_page(&page, 2u, 20u, true);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_import_page(fixture.core, &page));
    TEST_CHECK(fixture.repo->last_import_gap);
    TEST_CHECK(!fixture.core->import_continuity);
    TEST_CHECK_U64(21u, fixture.core->ack_cursor.event_seq);
    st_fill_page(&page, 2u, 30u, false);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_import_page(fixture.core, &page));
    TEST_CHECK(!fixture.core->import_continuity);
    TEST_CHECK_U64(31u, fixture.core->ack_cursor.event_seq);

    /* 仓储拒绝时水位不动（不能先把内存推进再说）。 */
    st_fill_page(&page, 2u, 40u, false);
    fixture.repo->import_status = LOCKER_STATUS_IO_ERROR;
    TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, domain_import_page(fixture.core, &page));
    fixture.repo->import_status = LOCKER_STATUS_OK;
    TEST_CHECK_U64(31u, fixture.core->ack_cursor.event_seq);

    /* 首帧本身带 gap：连续性从一开始就是 false。 */
    {
        dt_fixture_t second;
        locker_wire_fetch_events_response_t gapped;
        TEST_CHECK(dt_fixture_init(&second, false));
        dt_core_device_ready(&second, 3u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
        st_fill_page(&gapped, 1u, 7u, true);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_import_page(second.core, &gapped));
        TEST_CHECK(!second.core->import_continuity);
        dt_fixture_free(&second);
    }

    /* domain_ack_refresh 只跟仓储水位；没有水位时什么也不做。 */
    {
        locker_event_key_t before = fixture.core->ack_cursor;
        fixture.core->import_cursor.event_seq = 55u;
        domain_ack_refresh(fixture.core);
        TEST_CHECK_U64(55u, fixture.core->ack_cursor.event_seq);
        TEST_CHECK(before.event_seq != fixture.core->ack_cursor.event_seq);
        fixture.core->import_cursor_valid = false;
        domain_ack_refresh(fixture.core);
        TEST_CHECK_U64(55u, fixture.core->ack_cursor.event_seq);
        fixture.core->import_cursor_valid = true;
    }

    /* 导入游标记录只认第一条（首个有效记录生效）。 */
    {
        locker_json_document_t doc;
        char text[1024];
        snprintf(text, sizeof(text),
                 "{\"device_id\":\"" DT_DEVICE_ID "\","
                 "\"journal_epoch\":\"00000000-0000-4000-8000-000000000020\","
                 "\"through\":{\"device_id\":\"" DT_DEVICE_ID "\","
                 "\"journal_epoch\":\"00000000-0000-4000-8000-000000000020\","
                 "\"boot_id\":\"3\",\"event_seq\":\"77\"},\"continuity_ok\":false}");
        TEST_CHECK(st_parse_core(&fixture, &doc, text));
        fixture.core->import_cursor_valid = false;
        TEST_CHECK(domain_import_cursor_from_record(&doc, locker_domain_json_root(&doc), fixture.core));
        TEST_CHECK(fixture.core->import_cursor_valid);
        TEST_CHECK_U64(77u, fixture.core->import_cursor.event_seq);
        TEST_CHECK(!fixture.core->cursor.continuity_ok);
        TEST_CHECK(fixture.core->ack_cursor_valid);
        memset(&committed, 0, sizeof(committed));
        TEST_CHECK(domain_import_cursor_from_record(&doc, locker_domain_json_root(&doc), fixture.core));
        TEST_CHECK_U64(77u, fixture.core->import_cursor.event_seq);
        TEST_CHECK(!domain_import_cursor_from_record(&doc, NULL, fixture.core));
    }

    dt_fixture_free(&fixture);
}

static void test_persist_device_state(void)
{
    dt_fixture_t fixture;
    uint64_t first_revision;

    TEST_CHECK(dt_fixture_init(&fixture, false));
    dt_core_ready(&fixture);

    /* 没有可信快照 -> NOT_READY，不写占位行。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, domain_persist_device_state(fixture.core));
    TEST_CHECK(fixture.repo->commit_calls == 0u);

    dt_core_device_ready(&fixture, 3u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    fixture.core->phase = DOMAIN_PHASE_READY;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_persist_device_state(fixture.core));
    TEST_CHECK(fixture.repo->commit_calls == 1u);
    first_revision = fixture.core->revision;
    TEST_CHECK(dt_repo_find(fixture.repo, "device_state", DT_DEVICE_ID) != NULL);
    TEST_CHECK(fixture.core->persisted_state_valid);
    TEST_CHECK_U64(7u, fixture.core->persisted_physical_revision);
    TEST_CHECK(fixture.core->persisted_mode == LOCKER_DEVICE_MODE_READY);

    /* 四元组没变就不重复提交。 */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_persist_device_state(fixture.core));
    TEST_CHECK(fixture.repo->commit_calls == 1u);
    TEST_CHECK_U64(first_revision, fixture.core->revision);

    /* 物理修订变化才提交。 */
    fixture.core->physical.physical_revision = 8u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_persist_device_state(fixture.core));
    TEST_CHECK(fixture.repo->commit_calls == 2u);
    TEST_CHECK_U64(8u, fixture.core->persisted_physical_revision);

    /* 设备换过：旧身份行必须同事务删除。 */
    {
        char other_id[25];
        memcpy(other_id, "fedcba9876543210fedcba98", sizeof(other_id));
        memcpy(fixture.core->physical.device_id, other_id, sizeof(other_id));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_persist_device_state(fixture.core));
        TEST_CHECK(dt_repo_find(fixture.repo, "device_state", DT_DEVICE_ID) == NULL);
        TEST_CHECK(dt_repo_find(fixture.repo, "device_state", other_id) != NULL);
        TEST_CHECK_STRING(other_id, fixture.core->persisted_device_id);
    }

    /* 提交失败不更新持久化标记。 */
    fixture.core->physical.physical_revision = 9u;
    fixture.repo->commit_status = LOCKER_STATUS_CONFLICT;
    TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, domain_persist_device_state(fixture.core));
    fixture.repo->commit_status = LOCKER_STATUS_OK;
    TEST_CHECK_U64(8u, fixture.core->persisted_physical_revision);

    dt_fixture_free(&fixture);
}

int main(void)
{
    test_json_readers();
    test_operation_from_record();
    test_operation_load();
    test_principal_check();
    test_projections();
    test_response_envelope();
    test_writers();
    test_operation_state_machine();
    test_import_page();
    test_persist_device_state();
    TEST_REPORT("domain.test_domain_state");
}
