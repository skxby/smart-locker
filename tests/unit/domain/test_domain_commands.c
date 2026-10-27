#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：命令侧（申请 / 取消 / 确认 / 核验 / 柜前证明 / outbox / 超时）。
 *
 * 这里钉死的规则（对应任务卡与 internal-interfaces.md）：
 *   1. 两申请竞争只有一条活路：后到的明确 BUSY，不排队、不覆盖前者。
 *   2. 只有本人能确认/取消自己的操作；错误用户一律 INVALID。
 *   3. 提前确认（还在 REQUESTED/ACCESS_ACTIVE）一律 CONFLICT，不能跳步。
 *   4. 确认要同时满足物理终态与 item_transition_seen，缺一个就进 REVIEW_REQUIRED。
 *   5. 可能已执行的动作，取消只是核验请求（REVIEW_REQUIRED + 响应 CONFLICT）。
 *   6. 核验（resolve）只有 ADMIN、必须匹配 kind、门必须闭、物品必须明确。
 *   7. 到柜证明 15 秒过期：过期后重新进 REVIEW_REQUIRED，不能拿旧证明开门。
 *   8. outbox 先落「已尝试发送」检查点再发；检查点失败绝不发送。
 *   9. 发送后结果未知只查不重发；端口 BUSY 不算失败。
 *  10. 所有超时都有确定出口，且不会把 REVIEW_REQUIRED 再推回去。
 */

#include "domain_test_support.h"

#include <stdio.h>
#include <string.h>

/* ---------- 命令/查询执行辅助 ---------- */

static locker_command_t dc_command(const locker_principal_t *principal, const char *kind, const char *arguments,
                                   uint8_t request_seed, uint8_t idem_seed)
{
    locker_command_t command;
    memset(&command, 0, sizeof(command));
    command.principal = *principal;
    dt_uuid_fill(&command.request_id, request_seed);
    if (idem_seed != 0u) {
        dt_uuid_fill(&command.idempotency_key, idem_seed);
    }
    (void)snprintf(command.kind, sizeof(command.kind), "%s", kind);
    (void)snprintf(command.arguments_json, sizeof(command.arguments_json), "%s", arguments);
    return command;
}

/* 跑一条命令并取回终态；返回领域状态（不是 IO 状态）。 */
static locker_status_t dc_run_command(dt_fixture_t *fixture, const locker_command_t *command,
                                      locker_query_result_t *result)
{
    domain_job_t job;
    locker_uuid_t token;
    uint32_t slot;

    memset(&job, 0, sizeof(job));
    memset(result, 0, sizeof(*result));
    slot = domain_completion_acquire(fixture->core, &token);
    TEST_CHECK(slot != UINT32_MAX);
    if (slot == UINT32_MAX) {
        return LOCKER_STATUS_IO_ERROR;
    }
    job.kind = (uint8_t)DOMAIN_JOB_COMMAND;
    job.completion = slot;
    job.body.command = *command;
    domain_handle_command(fixture->core, &job);
    if (domain_completion_wait_final(fixture->core, slot, 0u, result) != LOCKER_STATUS_OK) {
        domain_completion_release(fixture->core, slot);
        return LOCKER_STATUS_NOT_READY;
    }
    domain_completion_release(fixture->core, slot);
    return result->status;
}

static locker_status_t dc_run_query(dt_fixture_t *fixture, const locker_query_t *query,
                                    locker_query_result_t *result)
{
    domain_job_t job;
    locker_uuid_t token;
    uint32_t slot;

    memset(&job, 0, sizeof(job));
    memset(result, 0, sizeof(*result));
    slot = domain_completion_acquire(fixture->core, &token);
    TEST_CHECK(slot != UINT32_MAX);
    if (slot == UINT32_MAX) {
        return LOCKER_STATUS_IO_ERROR;
    }
    job.kind = (uint8_t)DOMAIN_JOB_QUERY;
    job.completion = slot;
    job.body.query = *query;
    domain_handle_query(fixture->core, &job);
    if (domain_completion_wait_final(fixture->core, slot, 0u, result) != LOCKER_STATUS_OK) {
        domain_completion_release(fixture->core, slot);
        return LOCKER_STATUS_NOT_READY;
    }
    domain_completion_release(fixture->core, slot);
    return result->status;
}

/* 直接把仓储里的文档文本改一段，用来造「已满足物理证据」的夹具。 */
static bool dc_patch_doc(dt_fixture_t *fixture, const char *collection, const char *key, const char *from,
                         const char *to)
{
    dt_doc_t *doc = dt_repo_find(fixture->repo, collection, key);
    char buffer[DT_VALUE_BYTES];
    char *hit;
    size_t hit_offset;
    size_t tail_offset;

    if (doc == NULL) {
        return false;
    }
    hit = strstr(doc->value, from);
    if (hit == NULL) {
        return false;
    }
    hit_offset = (size_t)(hit - doc->value);
    tail_offset = hit_offset + strlen(from);
    if (hit_offset + strlen(to) + strlen(doc->value + tail_offset) + 1u > sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, doc->value, hit_offset);
    memcpy(buffer + hit_offset, to, strlen(to));
    memcpy(buffer + hit_offset + strlen(to), doc->value + tail_offset, strlen(doc->value + tail_offset) + 1u);
    return dt_repo_put(fixture->repo, collection, key, buffer);
}

/* 造一条已落库的操作并装进 core->active（等价于恢复期走的那条路径）。 */
static bool dc_seed_active(dt_fixture_t *fixture, uint8_t kind, uint8_t state, uint64_t revision,
                           const char *updated_at, const locker_uuid_t *id, const locker_uuid_t *tool_id,
                           const locker_uuid_t *actor_id)
{
    domain_operation_t operation;

    if (!dt_seed_operation(fixture, id, tool_id, actor_id, kind, state, revision, updated_at)) {
        return false;
    }
    memset(&operation, 0, sizeof(operation));
    if (domain_operation_load(fixture->core, id, 0, &operation) != LOCKER_STATUS_OK) {
        return false;
    }
    fixture->core->active = operation;
    return true;
}

static void dc_build_create_args(char *out, size_t out_size, const locker_uuid_t *tool_id, const char *kind,
                                 const char *tool_revision)
{
    char tool[LOCKER_DOMAIN_UUID_TEXT];
    dt_uuid_text(tool_id, tool, sizeof(tool));
    (void)snprintf(out, out_size, "{\"tool_id\":\"%s\",\"kind\":\"%s\",\"tool_revision\":\"%s\"}", tool,
                   kind, tool_revision);
}

/* ---------- OPERATION_CREATE 守卫 ---------- */

static void test_create_guards(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_query_result_t result;
    locker_uuid_t tool;
    char args[512];

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    dt_uuid_fill(&tool, 0x22u);
    dc_build_create_args(args, sizeof(args), &tool, "BORROW", "1");

    /* 参数缺失/畸形一律 INVALID（不能靠默认值蒙过去）。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, "", 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, "{}", 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    {
        /* kind 不是契约枚举。 */
        char bad[512];
        dc_build_create_args(bad, sizeof(bad), &tool, "STEAL", "1");
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, bad, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    {
        /* tool_revision 不是 U64 文本。 */
        char bad[512];
        dc_build_create_args(bad, sizeof(bad), &tool, "BORROW", "007");
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, bad, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }

    /* 未认证/会话不存在：principal 复检挡住，命令不落地。 */
    {
        locker_principal_t stranger = dt_principal(LOCKER_ROLE_USER, 0x74u);
        locker_command_t command = dc_command(&stranger, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }

    /* 工具 ID 必须是本柜格那一个。 */
    {
        locker_uuid_t other;
        char wrong_args[512];
        dt_uuid_fill(&other, 0x23u);
        dc_build_create_args(wrong_args, sizeof(wrong_args), &other, "BORROW", "1");
        fixture.core->tool_valid = true;
        fixture.core->tool_id = tool;
        {
            locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, wrong_args, 0x31u, 0x32u);
            TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
        }
        fixture.core->tool_valid = false;
    }

    /* 恢复未完成时一律 NOT_READY。 */
    {
        locker_command_t command;
        fixture.core->phase = DOMAIN_PHASE_RECOVERING;
        command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->phase = DOMAIN_PHASE_READY;
    }

    /* 幂等键是 OPERATION_CREATE 的硬要求。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }

    /* 未知命令不空实现冒充。 */
    {
        locker_command_t command = dc_command(&principal, "USERS_LIST", "{}", 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
    }

    /* 借出：已有有效借用 -> CONFLICT。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        fixture.core->loan_open = true;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        fixture.core->loan_open = false;
    }

    /* 借出：物品不在位 -> NOT_READY（不能开空柜）。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        fixture.core->physical.item = LOCKER_ITEM_STATE_ABSENT;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->physical.item = LOCKER_ITEM_STATE_PRESENT;
    }

    /* 借出：门开着 / 连续性缺口 / 降级 都不许起动作。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        fixture.core->physical.door = LOCKER_DOOR_STATE_OPEN;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->physical.door = LOCKER_DOOR_STATE_CLOSED;

        fixture.core->fetch_gap = true;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->fetch_gap = false;

        fixture.core->cursor.continuity_ok = false;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->cursor.continuity_ok = true;

        fixture.core->session.device_mode = LOCKER_DEVICE_MODE_DEGRADED;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->session.device_mode = LOCKER_DEVICE_MODE_READY;

        fixture.core->session.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->session.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;

        fixture.core->device_session_up = false;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->device_session_up = true;
    }

    /* 归还：没有开着借用 -> CONFLICT；非借用人 -> INVALID；物品在位 -> NOT_READY。 */
    {
        char return_args[512];
        locker_command_t command;
        dc_build_create_args(return_args, sizeof(return_args), &tool, "RETURN", "1");
        command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, return_args, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));

        fixture.core->loan_open = true;
        fixture.core->loan_id = tool;
        dt_uuid_fill(&fixture.core->loan_borrower_id, 0x71u);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));

        fixture.core->loan_borrower_id = principal.user_id;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
        fixture.core->physical.item = LOCKER_ITEM_STATE_ABSENT;
        /* 物品已取走，但这里只验证前置门；真正的成功路径在 test_create_success。 */
        fixture.core->loan_open = false;
        fixture.core->physical.item = LOCKER_ITEM_STATE_PRESENT;
    }

    dt_fixture_free(&fixture);
}

/* ---------- OPERATION_CREATE 成功路径 ---------- */

static void test_create_success(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_query_result_t result;
    locker_uuid_t tool;
    char args[512];
    char tool_text[LOCKER_DOMAIN_UUID_TEXT];
    uint32_t commit_before;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    dt_uuid_fill(&tool, 0x22u);
    dt_uuid_text(&tool, tool_text, sizeof(tool_text));
    dc_build_create_args(args, sizeof(args), &tool, "BORROW", "1");

    commit_before = fixture.repo->commit_calls;
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &result));
    }

    /* 一次事务：操作 + 占位 + 幂等记录，revision 只前进一步。 */
    TEST_CHECK_U64((uint64_t)commit_before + 1u, (uint64_t)fixture.repo->commit_calls);
    TEST_CHECK_U64(1u, fixture.core->revision);
    TEST_CHECK_U64(1u, fixture.repo->revision);
    TEST_CHECK(fixture.core->active.used);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REQUESTED);
    TEST_CHECK_U64(2u, fixture.core->active.revision);
    TEST_CHECK(locker_uuid_equal(&fixture.core->active.tool_id, &tool));
    TEST_CHECK(locker_uuid_equal(&fixture.core->active.actor_id, &principal.user_id));
    TEST_CHECK_U64(1000u + LOCKER_REQUEST_TTL_MS, fixture.core->active.requested_deadline_ms);
    /* 提交后的响应必须带提交后的版本，不是提交前的预估版本。 */
    TEST_CHECK_U64(1u, result.snapshot_revision);
    TEST_CHECK(test_text_contains(result.json, "\"state\":\"REQUESTED\""));
    TEST_CHECK(test_text_contains(result.json, tool_text));
    TEST_CHECK(test_text_contains(result.json, "\"snapshot_revision\":\"1\""));

    /* 占位与幂等记录都真的落库了。 */
    TEST_CHECK(dt_repo_find(fixture.repo, "slot_guards", "1") != NULL);
    {
        dt_doc_t *guard = dt_repo_find(fixture.repo, "slot_guards", "1");
        char operation_key[LOCKER_DOMAIN_UUID_TEXT];
        TEST_CHECK(guard != NULL);
        if (guard != NULL) {
            dt_uuid_text(&fixture.core->active.id, operation_key, sizeof(operation_key));
            TEST_CHECK(test_text_contains(guard->value, operation_key));
        }
    }
    {
        char idem_key[LOCKER_DOMAIN_UUID_TEXT];
        locker_uuid_t idem;
        dt_uuid_fill(&idem, 0x32u);
        dt_uuid_text(&idem, idem_key, sizeof(idem_key));
        {
            dt_doc_t *record = dt_repo_find(fixture.repo, "idempotency", idem_key);
            TEST_CHECK(record != NULL);
            if (record != NULL) {
                TEST_CHECK(test_text_contains(record->value, "\"response_status\":201"));
                TEST_CHECK(test_text_contains(record->value, "\"method\":\"POST\""));
                TEST_CHECK(test_text_contains(record->value, "OPERATION_CREATE"));
            }
        }
    }

    /* 申请被持久化后，reload 出来必须是同一份状态。 */
    {
        domain_operation_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &fixture.core->active.id, 1, &loaded));
        TEST_CHECK(loaded.state == LOCKER_OPERATION_STATE_REQUESTED);
        TEST_CHECK_U64(2u, loaded.revision);
        TEST_CHECK(locker_uuid_equal(&loaded.id, &fixture.core->active.id));
    }

    /* 有活操作时第二个申请明确 BUSY，且不产生新操作、不覆盖前者。 */
    {
        locker_uuid_t first = fixture.core->active.id;
        locker_query_result_t second;
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x33u, 0x34u);
        TEST_CHECK_STATUS(LOCKER_STATUS_BUSY, dc_run_command(&fixture, &command, &second));
        TEST_CHECK_U64(1u, fixture.core->revision);
        TEST_CHECK(locker_uuid_equal(&fixture.core->active.id, &first));
        TEST_CHECK_U64((uint64_t)commit_before + 1u, (uint64_t)fixture.repo->commit_calls);
    }

    /* 事件发布：resource 必须是契约枚举 operation，不是 "operation.changed"。 */
    TEST_CHECK(fixture.publisher->publish_calls >= 1u);
    TEST_CHECK_STRING("operation", fixture.publisher->last.resource);
    TEST_CHECK_U64(1u, fixture.publisher->last.snapshot_revision);

    dt_fixture_free(&fixture);
}

/* ---------- 幂等重放 ---------- */

static void test_create_idempotency(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_principal_t other;
    locker_query_result_t first;
    locker_query_result_t replay;
    locker_query_result_t conflict;
    locker_uuid_t tool;
    char args[512];
    char other_args[512];
    uint32_t commit_before;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    other = dt_principal(LOCKER_ROLE_USER, 0x76u);
    TEST_CHECK(dt_seed_principal(&fixture, &other));
    dt_uuid_fill(&tool, 0x22u);
    dc_build_create_args(args, sizeof(args), &tool, "BORROW", "1");
    dc_build_create_args(other_args, sizeof(other_args), &tool, "BORROW", "2");

    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &first));
    }
    commit_before = fixture.repo->commit_calls;
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x35u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &replay));
    }
    /* 重放：原响应原样返回，不产生新操作、不推进 revision。 */
    TEST_CHECK_STRING(first.json, replay.json);
    TEST_CHECK_U64(commit_before, (uint64_t)fixture.repo->commit_calls);
    TEST_CHECK_U64(1u, fixture.core->revision);

    /* 同一幂等键、不同请求体 -> CONFLICT（不能把两种意图当成一次）。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, other_args, 0x36u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &conflict));
    }
    /* 同一幂等键、不同用户 -> CONFLICT。 */
    {
        locker_command_t command = dc_command(&other, LOCKER_CORE_OPERATION_CREATE, args, 0x37u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &conflict));
    }
    TEST_CHECK_U64(commit_before, (uint64_t)fixture.repo->commit_calls);

    dt_fixture_free(&fixture);
}

/* ---------- 事务失败不留内存态 ---------- */

static void test_create_commit_failure(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_query_result_t result;
    locker_uuid_t tool;
    char args[512];

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    dt_uuid_fill(&tool, 0x22u);
    dc_build_create_args(args, sizeof(args), &tool, "BORROW", "1");

    fixture.repo->commit_status = LOCKER_STATUS_IO_ERROR;
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x31u, 0x32u);
        TEST_CHECK_STATUS(LOCKER_STATUS_IO_ERROR, dc_run_command(&fixture, &command, &result));
    }
    /* 事务没成功：既没有活动操作，也没有占位/幂等残留，revision 不动。 */
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK_U64(0u, fixture.core->revision);
    TEST_CHECK(dt_repo_find(fixture.repo, "slot_guards", "1") == NULL);
    {
        char idem_key[LOCKER_DOMAIN_UUID_TEXT];
        locker_uuid_t idem;
        dt_uuid_fill(&idem, 0x32u);
        dt_uuid_text(&idem, idem_key, sizeof(idem_key));
        TEST_CHECK(dt_repo_find(fixture.repo, "idempotency", idem_key) == NULL);
    }
    /* 失败后下一次仍然能正常申请（不是把 Core 卡死）。 */
    fixture.repo->commit_status = LOCKER_STATUS_OK;
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CREATE, args, 0x38u, 0x39u);
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &result));
    }
    TEST_CHECK(fixture.core->active.used);

    dt_fixture_free(&fixture);
}

/* ---------- OPERATION_CONFIRM ---------- */

static void test_confirm(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_principal_t other;
    locker_query_result_t result;
    locker_uuid_t id;
    locker_uuid_t tool;
    char args[128];

    TEST_CHECK(dt_fixture_init(&fixture, true));
    /* 借出完成后物品应当已被取走：item=ABSENT 才是「物理已到终态」。 */
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    other = dt_principal(LOCKER_ROLE_USER, 0x78u);
    TEST_CHECK(dt_seed_principal(&fixture, &other));
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x22u);
    (void)snprintf(args, sizeof(args), "{\"operation_revision\":\"4\"}");

    /* 记录不存在 / 版本不符 / 未到 WAIT_CONFIRM / 非本人 都不许确认。 */
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_WAIT_CONFIRM, 4u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        locker_uuid_t missing;
        locker_command_t command;
        dt_uuid_fill(&missing, 0x52u);
        command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM, args, 0x31u, 0u);
        command.resource_id = missing;
        TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_command(&fixture, &command, &result));
    }
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM, args, 0x31u, 0u);
        command.resource_id = id;
        command.principal = other;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM,
                                              "{\"operation_revision\":\"3\"}", 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
    }
    {
        /* 参数缺失：没有 operation_revision 就一律 INVALID。 */
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM, "{}", 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }

    /* 物理证据不全（item_transition_seen=false）-> 进核验而不是完成。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM, args, 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
        TEST_CHECK_STRING("CONFIRM_PRECONDITION", fixture.core->active.reason_code);
    }

    /* 补上物理变化证据后可以确认：操作终结 + 新建 loan，两步各自一个事务。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_WAIT_CONFIRM, 4u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        char key[LOCKER_DOMAIN_UUID_TEXT];
        dt_uuid_text(&id, key, sizeof(key));
        TEST_CHECK(dc_patch_doc(&fixture, "operations", key, "\"item_transition_seen\":false",
                                "\"item_transition_seen\":true"));
    }
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM, args, 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &result));
    }
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK(fixture.core->loan_open);
    TEST_CHECK(locker_uuid_equal(&fixture.core->loan_borrower_id, &principal.user_id));
    TEST_CHECK(locker_uuid_equal(&fixture.core->loan_tool_id, &tool));
    TEST_CHECK_U64(2u, fixture.core->revision);
    {
        domain_operation_t loaded;
        char key[LOCKER_DOMAIN_UUID_TEXT];
        dt_uuid_text(&id, key, sizeof(key));
        memset(&loaded, 0, sizeof(loaded));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
        TEST_CHECK(loaded.state == LOCKER_OPERATION_STATE_COMPLETED);
        TEST_CHECK_STRING("CONFIRMED", loaded.reason_code);
        TEST_CHECK(loaded.has_loan);
        TEST_CHECK(dt_repo_find(fixture.repo, "slot_guards", "1") == NULL);
        {
            char loan_key[LOCKER_DOMAIN_UUID_TEXT];
            dt_uuid_text(&loaded.related_loan_id, loan_key, sizeof(loan_key));
            TEST_CHECK(dt_repo_find(fixture.repo, "loans", loan_key) != NULL);
        }
    }
    /* 借出确认发的是 operation 与 loan 两种资源变更。 */
    TEST_CHECK(fixture.publisher->publish_calls >= 2u);
    TEST_CHECK_STRING("loan", fixture.publisher->last.resource);

    /* 提前确认：还在 ACCESS_ACTIVE 的一律 CONFLICT（不能跳步）。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 4u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CONFIRM, args, 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_ACCESS_ACTIVE);
    }
    dt_fixture_free(&fixture);
}

/* ---------- OPERATION_CANCEL ---------- */

static void test_cancel(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_principal_t other;
    locker_query_result_t result;
    locker_uuid_t id;
    locker_uuid_t tool;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    other = dt_principal(LOCKER_ROLE_USER, 0x7au);
    TEST_CHECK(dt_seed_principal(&fixture, &other));
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x22u);

    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_REQUESTED, 2u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    /* 必须给理由：无理由的取消不记录就没有可审计的因果。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CANCEL,
                                              "{\"operation_revision\":\"2\"}", 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    /* 非本人 -> INVALID。 */
    {
        locker_command_t command = dc_command(&other, LOCKER_CORE_OPERATION_CANCEL,
                                              "{\"operation_revision\":\"2\",\"reason\":\"changed mind\"}", 0x31u,
                                              0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    /* 还不算太晚：REQUESTED 可以直接取消并释放占位。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CANCEL,
                                              "{\"operation_revision\":\"2\",\"reason\":\"changed mind\"}", 0x31u,
                                              0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &result));
    }
    TEST_CHECK(!fixture.core->active.used);
    TEST_CHECK(dt_repo_find(fixture.repo, "slot_guards", "1") == NULL);
    {
        domain_operation_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
        TEST_CHECK(loaded.state == LOCKER_OPERATION_STATE_CANCELLED);
        TEST_CHECK_STRING("CANCELLED", loaded.reason_code);
    }
    /* 终态再取消 -> CONFLICT。 */
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CANCEL,
                                              "{\"operation_revision\":\"3\",\"reason\":\"again\"}", 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
    }

    /* 已经发过动作意图：取消只能变成核验请求，绝不声称取消成功。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_ACCESS_PENDING, 5u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        locker_command_t command = dc_command(&principal, LOCKER_CORE_OPERATION_CANCEL,
                                              "{\"operation_revision\":\"5\",\"reason\":\"too late\"}", 0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
        TEST_CHECK_STRING("CANCEL_AFTER_ACTION", fixture.core->active.reason_code);
    }
    dt_fixture_free(&fixture);
}

/* ---------- OPERATION_RESOLVE（管理员核验） ---------- */

static void test_resolve(void)
{
    dt_fixture_t fixture;
    locker_principal_t admin;
    locker_principal_t user;
    locker_query_result_t result;
    locker_uuid_t id;
    locker_uuid_t tool;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    admin = dt_principal(LOCKER_ROLE_ADMIN, 0x80u);
    TEST_CHECK(dt_seed_principal(&fixture, &admin));
    user = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &user));
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x22u);

    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_REVIEW_REQUIRED, 6u,
                              "2025-10-21T00:00:00Z", &id, &tool, &user.user_id));

    /* 普通用户不能核验：不允许管理员无理由覆盖的反面也一样——越权一律 INVALID。 */
    {
        locker_command_t command = dc_command(&user, LOCKER_CORE_OPERATION_RESOLVE,
                                              "{\"operation_revision\":\"6\",\"physical_revision\":\"7\","
                                              "\"resolution\":\"COMPLETE_BORROW\",\"reason\":\"operator checked\"}",
                                              0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    /* 理由太短 -> INVALID（核验必须留下可审计的因果）。 */
    {
        locker_command_t command = dc_command(&admin, LOCKER_CORE_OPERATION_RESOLVE,
                                              "{\"operation_revision\":\"6\",\"physical_revision\":\"7\","
                                              "\"resolution\":\"COMPLETE_BORROW\",\"reason\":\"abc\"}",
                                              0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    /* resolution 与 kind 不匹配 -> INVALID。 */
    {
        locker_command_t command = dc_command(&admin, LOCKER_CORE_OPERATION_RESOLVE,
                                              "{\"operation_revision\":\"6\",\"physical_revision\":\"7\","
                                              "\"resolution\":\"COMPLETE_RETURN\",\"reason\":\"operator checked\"}",
                                              0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_command(&fixture, &command, &result));
    }
    /* 门开着 / 物品 UNKNOWN / 物理版本不符 都拒绝，不能强行结案。 */
    {
        locker_command_t command = dc_command(&admin, LOCKER_CORE_OPERATION_RESOLVE,
                                              "{\"operation_revision\":\"6\",\"physical_revision\":\"7\","
                                              "\"resolution\":\"COMPLETE_BORROW\",\"reason\":\"operator checked\"}",
                                              0x31u, 0u);
        command.resource_id = id;
        fixture.core->physical.door = LOCKER_DOOR_STATE_OPEN;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        fixture.core->physical.door = LOCKER_DOOR_STATE_CLOSED;

        fixture.core->physical.item = LOCKER_ITEM_STATE_UNKNOWN;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        fixture.core->physical.item = LOCKER_ITEM_STATE_ABSENT;

        fixture.core->physical.physical_revision = 8u;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
        fixture.core->physical.physical_revision = 7u;

        /* 版本落后也拒绝。 */
        command = dc_command(&admin, LOCKER_CORE_OPERATION_RESOLVE,
                             "{\"operation_revision\":\"5\",\"physical_revision\":\"7\","
                             "\"resolution\":\"COMPLETE_BORROW\",\"reason\":\"operator checked\"}",
                             0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_CONFLICT, dc_run_command(&fixture, &command, &result));
    }
    /* 正常核验完成借出：终结 + 新建 loan。 */
    {
        locker_command_t command = dc_command(&admin, LOCKER_CORE_OPERATION_RESOLVE,
                                              "{\"operation_revision\":\"6\",\"physical_revision\":\"7\","
                                              "\"resolution\":\"COMPLETE_BORROW\",\"reason\":\"operator checked\"}",
                                              0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &result));
    }
    TEST_CHECK(fixture.core->loan_open);
    TEST_CHECK(locker_uuid_equal(&fixture.core->loan_borrower_id, &user.user_id));
    {
        domain_operation_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
        TEST_CHECK(loaded.state == LOCKER_OPERATION_STATE_COMPLETED);
        TEST_CHECK_STRING("ADMIN_RESOLVED", loaded.reason_code);
        TEST_CHECK(loaded.manual_resolution);
    }

    /* NO_CHANGE：核验为「什么都没发生」，不建 loan、不写账本。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &admin));
    TEST_CHECK(dt_seed_principal(&fixture, &user));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_REVIEW_REQUIRED, 3u,
                              "2025-10-21T00:00:00Z", &id, &tool, &user.user_id));
    {
        locker_command_t command = dc_command(&admin, LOCKER_CORE_OPERATION_RESOLVE,
                                              "{\"operation_revision\":\"3\",\"physical_revision\":\"7\","
                                              "\"resolution\":\"NO_CHANGE\",\"reason\":\"door never opened\"}",
                                              0x31u, 0u);
        command.resource_id = id;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_command(&fixture, &command, &result));
    }
    TEST_CHECK(!fixture.core->loan_open);
    {
        domain_operation_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
        TEST_CHECK(loaded.state == LOCKER_OPERATION_STATE_NO_CHANGE);
    }
    dt_fixture_free(&fixture);
}

/* ---------- 柜前证明 + 起动作（arm） ---------- */

static void test_presence_and_arm(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_principal_t other;
    locker_presence_t presence;
    locker_peer_t peer;
    locker_uuid_t id;
    locker_uuid_t tool;
    domain_job_t job;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    other = dt_principal(LOCKER_ROLE_USER, 0x7cu);
    TEST_CHECK(dt_seed_principal(&fixture, &other));
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x22u);
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_REQUESTED, 2u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));

    memset(&peer, 0, sizeof(peer));
    peer.verified_ui_peer = true;
    memset(&presence, 0, sizeof(presence));
    presence.principal = principal;
    presence.operation_id = id;
    presence.operation_revision = 2u;

    /* 非本人提交到柜证明 -> 不接受（证明必须属于操作本人）。 */
    {
        locker_presence_t wrong = presence;
        wrong.principal = other;
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_PRESENCE;
        job.completion = UINT32_MAX;
        job.body.presence.peer = peer;
        job.body.presence.presence = wrong;
        domain_handle_presence(fixture.core, &job);
        TEST_CHECK(!fixture.core->active.proof_consumed);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REQUESTED);
    }
    /* 版本不符也不接受。 */
    {
        locker_presence_t stale = presence;
        stale.operation_revision = 1u;
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_PRESENCE;
        job.completion = UINT32_MAX;
        job.body.presence.peer = peer;
        job.body.presence.presence = stale;
        domain_handle_presence(fixture.core, &job);
        TEST_CHECK(!fixture.core->active.proof_consumed);
    }

    /* 正常到柜：证明被接纳，并且立刻推进到 ACCESS_PENDING + 写 outbox + 发一次 EXECUTE。 */
    {
        uint32_t commit_before = fixture.repo->commit_calls;
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_PRESENCE;
        job.completion = UINT32_MAX;
        job.body.presence.peer = peer;
        job.body.presence.presence = presence;
        domain_handle_presence(fixture.core, &job);
        TEST_CHECK(fixture.core->active.proof_consumed);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_ACCESS_PENDING);
        TEST_CHECK_STRING("ARMED", fixture.core->active.reason_code);
        TEST_CHECK(fixture.core->active.outbox_present);
        TEST_CHECK(fixture.core->active.action_no == 1u);
        TEST_CHECK_U64(2u, fixture.core->next_action_no);
        TEST_CHECK_U64(1000u, fixture.core->active.access_pending_ms);
        TEST_CHECK_U64(1000u + LOCKER_PREPARE_DEADLINE_MS, fixture.core->active.outbox_not_after_ms);
        TEST_CHECK(locker_uuid_equal(&fixture.core->active.outbox_session_id, &fixture.core->session.session_id));
        TEST_CHECK_U64(fixture.core->session.boot_id, fixture.core->active.outbox_boot_id);
        TEST_CHECK_U64(7u, fixture.core->active.outbox_physical_revision);
        /* arm 提交一次；随后 outbox 泵的检查点再提交一次，最后才发 EXECUTE。 */
        TEST_CHECK(fixture.repo->commit_calls > commit_before);
        TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
        TEST_CHECK(fixture.device->has_intent);
        if (fixture.device->has_intent) {
            TEST_CHECK_U64(1u, fixture.device->last_intent.action_no);
            TEST_CHECK(locker_uuid_equal(&fixture.device->last_intent.operation_id, &id));
            TEST_CHECK(locker_uuid_equal(&fixture.device->last_intent.command_id, &fixture.core->active.command_id));
            TEST_CHECK(fixture.device->last_intent.kind == LOCKER_OPERATION_KIND_BORROW);
            TEST_CHECK_U64(1000u + LOCKER_LOCK_PULSE_MAX_MS, fixture.device->last_intent.not_after_mcu_ms);
            TEST_CHECK_U64(1u, (uint64_t)fixture.device->last_intent.slot_id);
        }
        TEST_CHECK(fixture.core->active.send_started);
        TEST_CHECK(fixture.core->active.outbox_state == 2u);
    }
    /* outbox 记录已经落库，且状态是 SEND_ATTEMPTED。 */
    {
        char outbox_key[LOCKER_DOMAIN_UUID_TEXT];
        dt_uuid_text(&fixture.core->active.outbox_id, outbox_key, sizeof(outbox_key));
        dt_doc_t *outbox = dt_repo_find(fixture.repo, "outbox", outbox_key);
        TEST_CHECK(outbox != NULL);
        if (outbox != NULL) {
            TEST_CHECK(test_text_contains(outbox->value, "\"state\":\"SEND_ATTEMPTED\""));
            TEST_CHECK(test_text_contains(outbox->value, "\"slot_id\":1"));
        }
    }

    /* 同一个操作第二次到柜：已不是 REQUESTED，不再接受新证明。 */
    {
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_PRESENCE;
        job.completion = UINT32_MAX;
        job.body.presence.peer = peer;
        job.body.presence.presence = presence;
        domain_handle_presence(fixture.core, &job);
        TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
    }

    /* 15 秒证明过期：重新进 REVIEW_REQUIRED，绝不拿旧证明开门。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_REQUESTED, 2u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        /*
         * 证明先被接纳，但这一刻门的物理条件不成立（门开着），所以操作只能停在
         * REQUESTED，证明留在 active 上；把时钟推过 15 秒后由 tick 触发的 arm
         * 必须把旧证明判过期并转 REVIEW_REQUIRED，而不是继续拿它去开门。
         */
        locker_presence_t early = presence;
        fixture.core->physical.door = LOCKER_DOOR_STATE_OPEN;
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_PRESENCE;
        job.completion = UINT32_MAX;
        job.body.presence.peer = peer;
        job.body.presence.presence = early;
        domain_handle_presence(fixture.core, &job);
        TEST_CHECK(fixture.core->active.proof_consumed);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REQUESTED);
        TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);

        /* 门关上了、但证明已经放了 15 秒以上：不得用它开门。 */
        fixture.core->physical.door = LOCKER_DOOR_STATE_CLOSED;
        domain_handle_tick(fixture.core, 1000u + LOCKER_PRESENCE_TTL_MS + 1u);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
        TEST_CHECK_STRING("PRESENCE_EXPIRED", fixture.core->active.reason_code);
        TEST_CHECK(!fixture.core->active.proof_consumed);
        TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);

        /* 过期转核验之后，同一个操作再提交到柜证明也不再被接受。 */
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_PRESENCE;
        job.completion = UINT32_MAX;
        job.body.presence.peer = peer;
        job.body.presence.presence = early;
        domain_handle_presence(fixture.core, &job);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
        TEST_CHECK(!fixture.core->active.proof_consumed);
        TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    }

    dt_fixture_free(&fixture);
}

/* ---------- outbox 泵 ---------- */

/*
 * 造一条停在 ACCESS_PENDING、已冻结 outbox 但是还没真正发送的操作：
 * 直接落到「检查点已写好、EXECUTE 未发出」这一刻，用来单独验证泵的每道门。
 */
static bool dc_seed_pending(dt_fixture_t *fixture, const locker_uuid_t *id, const locker_uuid_t *tool,
                            const locker_principal_t *principal)
{
    if (!dc_seed_active(fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_ACCESS_PENDING, 5u,
                        "2025-10-21T00:00:00Z", id, tool, &principal->user_id)) {
        return false;
    }
    fixture->core->active.outbox_present = true;
    fixture->core->active.has_command = true;
    dt_uuid_fill(&fixture->core->active.command_id, 0x92u);
    dt_uuid_fill(&fixture->core->active.outbox_id, 0x91u);
    fixture->core->active.action_no = fixture->core->next_action_no;
    fixture->core->active.outbox_state = 1u; /* PREPARED */
    fixture->core->active.send_started = false;
    fixture->core->active.outbox_session_id = fixture->core->session.session_id;
    fixture->core->active.outbox_journal_epoch = fixture->core->session.journal_epoch;
    fixture->core->active.outbox_boot_id = fixture->core->session.boot_id;
    fixture->core->active.outbox_physical_revision = fixture->core->physical.physical_revision;
    fixture->core->active.outbox_not_after_ms = 1000u + LOCKER_PREPARE_DEADLINE_MS;
    fixture->core->active.access_pending_ms = 1000u;
    /* 真实的 ACCESS_PENDING 只可能由 operation_arm 推进得到，那时 reason_code 就是
       "ARMED"；直接落库的夹具记录里是 null，这里补成与真实路径一致的值。 */
    (void)domain_text_copy(fixture->core->active.reason_code, sizeof(fixture->core->active.reason_code), "ARMED");
    return true;
}

static void test_outbox_pump(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_uuid_t id;
    locker_uuid_t tool;

    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x22u);

    /* 准备期限已过：不发送，转核验。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    (void)domain_now_advance(fixture.core, fixture.core->active.outbox_not_after_ms + 1u);
    domain_outbox_pump(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("PREPARE_DEADLINE", fixture.core->active.reason_code);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    TEST_CHECK(!fixture.core->active.send_started);
    dt_fixture_free(&fixture);

    /* 会话换了（boot 前进或 journal_epoch 变化）：不发送，转核验。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    fixture.core->active.outbox_boot_id = 4u;
    domain_outbox_pump(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("SESSION_CHANGED", fixture.core->active.reason_code);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    dt_fixture_free(&fixture);

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    dt_uuid_fill(&fixture.core->active.outbox_journal_epoch, 0x2bu);
    domain_outbox_pump(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("SESSION_CHANGED", fixture.core->active.reason_code);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    dt_fixture_free(&fixture);

    /* 物理版本前进（门被动过）：不发送，转核验。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    fixture.core->physical.physical_revision = 8u;
    domain_outbox_pump(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("PHYSICAL_CHANGED", fixture.core->active.reason_code);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    dt_fixture_free(&fixture);

    /* 检查点提交失败：一个字节都不许发出去。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    fixture.repo->commit_status = LOCKER_STATUS_IO_ERROR;
    domain_outbox_pump(fixture.core);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    TEST_CHECK(!fixture.device->has_intent);
    TEST_CHECK(!fixture.core->active.send_started);
    TEST_CHECK(fixture.core->active.outbox_state == 1u);
    dt_fixture_free(&fixture);

    /* 门都过了：先提交 SEND_ATTEMPTED 检查点，再发一次 EXECUTE。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    {
        uint32_t commits_before = fixture.repo->commit_calls;
        domain_outbox_pump(fixture.core);
        TEST_CHECK(fixture.repo->commit_calls == commits_before + 1u);
        TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
        TEST_CHECK(fixture.device->has_intent);
        if (fixture.device->has_intent) {
            TEST_CHECK_U64(fixture.core->active.action_no, fixture.device->last_intent.action_no);
            TEST_CHECK(locker_uuid_equal(&fixture.device->last_intent.command_id,
                                         &fixture.core->active.command_id));
            TEST_CHECK_U64(1000u + LOCKER_LOCK_PULSE_MAX_MS, fixture.device->last_intent.not_after_mcu_ms);
        }
        TEST_CHECK(fixture.core->active.send_started);
        TEST_CHECK(fixture.core->active.outbox_state == 2u);
        TEST_CHECK_U64(1000u, fixture.core->active.send_started_ms);
        /* 已经在途：再泵一次不会重发第二条。 */
        fixture.core->execute_in_flight = false;
        fixture.core->active.send_started = true;
        domain_outbox_pump(fixture.core);
        TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
    }
    dt_fixture_free(&fixture);

    /* 端口本身拒绝（非 BUSY）：结果未知，转核验而不是重发。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    fixture.device->execute_status = LOCKER_STATUS_IO_ERROR;
    domain_outbox_pump(fixture.core);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("SEND_FAILED", fixture.core->active.reason_code);
    /* 已经进核验之后，同样的泵再跑一次不会再发第二条。 */
    domain_outbox_pump(fixture.core);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
    dt_fixture_free(&fixture);

    /* 端口回 BUSY（通道还没消化上一条）：保持 ACCESS_PENDING，不转核验也不重发。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_pending(&fixture, &id, &tool, &principal));
    fixture.device->execute_status = LOCKER_STATUS_BUSY;
    domain_outbox_pump(fixture.core);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_ACCESS_PENDING);
    TEST_CHECK_STRING("ARMED", fixture.core->active.reason_code);
    /* 检查点已经落库（发送已尝试），所以这里必须是 true —— 状态要如实反映「可能已经发出」。 */
    TEST_CHECK(fixture.core->active.send_started);
    TEST_CHECK(fixture.core->active.outbox_state == 2u);
    domain_outbox_pump(fixture.core);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->execute_calls);
    dt_fixture_free(&fixture);
}

/* ---------- 超时 ---------- */

static void test_timeouts(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_uuid_t id;
    locker_uuid_t tool;

    /* REQUESTED 到期：取消并释放占位。 */
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    dt_uuid_fill(&id, 0x51u);
    dt_uuid_fill(&tool, 0x22u);
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_REQUESTED, 2u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        uint64_t deadline = fixture.core->active.requested_deadline_ms;
        TEST_CHECK(deadline != 0u);
        (void)domain_now_advance(fixture.core, deadline - 1u);
        domain_timeouts(fixture.core);
        TEST_CHECK(fixture.core->active.used);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REQUESTED);
        (void)domain_now_advance(fixture.core, deadline);
        domain_timeouts(fixture.core);
        TEST_CHECK(!fixture.core->active.used);
    }
    {
        domain_operation_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_operation_load(fixture.core, &id, 1, &loaded));
        TEST_CHECK(loaded.state == LOCKER_OPERATION_STATE_CANCELLED);
        TEST_CHECK_STRING("REQUEST_EXPIRED", loaded.reason_code);
    }

    /* ACCESS_PENDING 到期：开门超时进核验。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_ACCESS_PENDING, 5u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    fixture.core->active.access_pending_ms = 1000u;
    (void)domain_now_advance(fixture.core, 1000u + LOCKER_WAIT_OPEN_MS - 1u);
    domain_timeouts(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_ACCESS_PENDING);
    (void)domain_now_advance(fixture.core, 1000u + LOCKER_WAIT_OPEN_MS);
    domain_timeouts(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("OPEN_TIMEOUT", fixture.core->active.reason_code);

    /* ACCESS_ACTIVE 到期：关门超时进核验。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_OPEN, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_ACCESS_ACTIVE, 5u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    fixture.core->active.door_opened_ms = 1000u;
    (void)domain_now_advance(fixture.core, 1000u + LOCKER_WAIT_CLOSE_MS);
    domain_timeouts(fixture.core);
    TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
    TEST_CHECK_STRING("CLOSE_TIMEOUT", fixture.core->active.reason_code);

    /* WAIT_CONFIRM 到期：确认超时进核验。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_ABSENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_WAIT_CONFIRM, 5u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    {
        uint64_t deadline = fixture.core->active.wait_confirm_deadline_ms;
        TEST_CHECK(deadline != 0u);
        (void)domain_now_advance(fixture.core, deadline);
        domain_timeouts(fixture.core);
        TEST_CHECK(fixture.core->active.state == LOCKER_OPERATION_STATE_REVIEW_REQUIRED);
        TEST_CHECK_STRING("CONFIRM_TIMEOUT", fixture.core->active.reason_code);
    }

    /* 命令结果未知：只查询一次，绝不重发；查询过就不再重复查。 */
    dt_fixture_free(&fixture);
    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    TEST_CHECK(dc_seed_active(&fixture, LOCKER_OPERATION_KIND_BORROW, LOCKER_OPERATION_STATE_ACCESS_PENDING, 5u,
                              "2025-10-21T00:00:00Z", &id, &tool, &principal.user_id));
    fixture.core->active.send_started = true;
    fixture.core->pending_command_valid = true;
    fixture.core->pending_command_ms = 1000u;
    fixture.core->pending_command_queried = false;
    dt_uuid_fill(&fixture.core->pending_command_id, 0x66u);
    (void)domain_now_advance(fixture.core, 1000u + LOCKER_HOST_TIMEOUT_MS);
    domain_timeouts(fixture.core);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->query_calls);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->execute_calls);
    TEST_CHECK(fixture.core->pending_command_queried);
    domain_timeouts(fixture.core);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->query_calls);
    /* 超过窗口且没有在途执行：清掉待确认命令号，不再等待。 */
    (void)domain_now_advance(fixture.core, 1000u + LOCKER_HOST_TIMEOUT_MS + LOCKER_HOST_TIMEOUT_MS);
    domain_timeouts(fixture.core);
    TEST_CHECK(!fixture.core->pending_command_valid);
    TEST_CHECK(!fixture.core->pending_command_queried);

    /* 在途请求槽超时：按用途把 in_flight 复位，避免永久卡死。 */
    {
        fixture.core->snapshot_in_flight = true;
        fixture.core->pending[0].used = true;
        fixture.core->pending[0].purpose = DOMAIN_PURPOSE_SNAPSHOT;
        fixture.core->pending[0].started_ms = 1000u;
        (void)domain_now_advance(fixture.core, 1000u + LOCKER_HOST_TIMEOUT_MS);
        domain_timeouts(fixture.core);
        TEST_CHECK(!fixture.core->pending[0].used);
        TEST_CHECK(!fixture.core->snapshot_in_flight);
    }

    dt_fixture_free(&fixture);
}

/* ---------- 查询分派 ---------- */

static void test_query_dispatch(void)
{
    dt_fixture_t fixture;
    locker_principal_t principal;
    locker_query_t query;
    locker_query_result_t result;
    locker_uuid_t tool;
    locker_uuid_t id;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    dt_uuid_fill(&tool, 0x22u);
    dt_uuid_fill(&id, 0x51u);

    memset(&query, 0, sizeof(query));
    query.principal = principal;
    dt_uuid_fill(&query.request_id, 0x90u);
    query.limit = 20u;

    /* 未认证：不返回任何业务数据。 */
    {
        locker_query_t broken = query;
        broken.principal.authenticated = false;
        (void)snprintf(broken.kind, sizeof(broken.kind), "%s", LOCKER_CORE_DEVICE_GET);
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_query(&fixture, &broken, &result));
    }

    /* DEVICE_GET：带契约版本的设备投影。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_DEVICE_GET);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"contract_version\":\"0.2.1\""));
    TEST_CHECK(test_text_contains(result.json, "\"mode\":\"READY\""));
    TEST_CHECK(test_text_contains(result.json, "\"simulated\":false"));
    TEST_CHECK(test_text_contains(result.json, "test-cabinet"));
    TEST_CHECK(test_text_contains(result.json, DT_DEVICE_ID));

    /* SNAPSHOT_GET：四个分区都在，且没有就没有伪造成空对象。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_SNAPSHOT_GET);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"device\":"));
    TEST_CHECK(test_text_contains(result.json, "\"tools\":[]"));
    TEST_CHECK(test_text_contains(result.json, "\"active_operations\":[]"));
    TEST_CHECK(test_text_contains(result.json, "\"loans\":[]"));

    /* TOOLS_LIST 没有工具时是空数组；TOOL_GET 要匹配本柜格的工具 ID。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_TOOLS_LIST);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"items\":[]"));
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_TOOL_GET);
    query.resource_id = tool;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, dc_run_query(&fixture, &query, &result));
    fixture.core->tool_valid = true;
    fixture.core->tool_id = tool;
    (void)snprintf(fixture.core->tool_name, sizeof(fixture.core->tool_name), "%s", "shovel");
    fixture.core->tool_revision = 3u;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"availability\":\"AVAILABLE\""));
    TEST_CHECK(test_text_contains(result.json, "shovel"));

    /* OPERATION_GET 命中与未命中。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_OPERATION_GET);
    query.resource_id = id;
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(dt_seed_operation(&fixture, &id, &tool, &principal.user_id, LOCKER_OPERATION_KIND_BORROW,
                                 LOCKER_OPERATION_STATE_REQUESTED, 2u, "2025-10-21T00:00:00Z"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"state\":\"REQUESTED\""));
    TEST_CHECK(test_text_contains(result.json, "\"operation_revision\":\"2\""));

    /* REVIEWS_LIST 只回核验队列；EVENTS_LIST 阶段 1 是空页，不伪造历史。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_REVIEWS_LIST);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"items\":[]"));
    {
        locker_uuid_t review_id;
        dt_uuid_fill(&review_id, 0x53u);
        TEST_CHECK(dt_seed_operation(&fixture, &review_id, &tool, &principal.user_id, LOCKER_OPERATION_KIND_BORROW,
                                     LOCKER_OPERATION_STATE_REVIEW_REQUIRED, 3u, "2025-10-21T00:00:00Z"));
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"state\":\"REVIEW_REQUIRED\""));
    /* 非核验状态不进 REVIEWS_LIST。 */
    TEST_CHECK(!test_text_contains(result.json, "\"state\":\"REQUESTED\""));

    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_EVENTS_LIST);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"items\":[]"));
    TEST_CHECK(test_text_contains(result.json, "\"next_cursor\":null"));

    /* LOANS_LIST：没有有效借用是空数组，有借用时回当前记录。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_LOANS_LIST);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"items\":[]"));
    fixture.core->loan_open = true;
    fixture.core->loan_id = tool;
    fixture.core->loan_tool_id = tool;
    fixture.core->loan_borrower_id = principal.user_id;
    fixture.core->loan_borrow_operation_id = id;
    (void)snprintf(fixture.core->loan_opened_at, sizeof(fixture.core->loan_opened_at), "%s",
                   "2025-10-21T00:00:00Z");
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, dc_run_query(&fixture, &query, &result));
    TEST_CHECK(test_text_contains(result.json, "\"return_operation_id\":null"));
    TEST_CHECK(test_text_contains(result.json, "\"closed_at\":null"));

    /* 归 S1-05/S2 的命令不空实现冒充。 */
    (void)snprintf(query.kind, sizeof(query.kind), "%s", LOCKER_CORE_SESSION_CURRENT);
    TEST_CHECK_STATUS(LOCKER_STATUS_NOT_READY, dc_run_query(&fixture, &query, &result));

    dt_fixture_free(&fixture);
}

/* ---------- AUTH / 维护 / tick 分派 ---------- */

static void test_job_dispatch(void)
{
    dt_fixture_t fixture;
    locker_principal_t admin;
    locker_principal_t principal;
    domain_job_t job;
    locker_uuid_t token;
    uint32_t slot;

    TEST_CHECK(dt_fixture_init(&fixture, true));
    dt_core_device_ready(&fixture, 5u, LOCKER_DOOR_STATE_CLOSED, LOCKER_ITEM_STATE_PRESENT);
    principal = dt_principal(LOCKER_ROLE_USER, 0x70u);
    TEST_CHECK(dt_seed_principal(&fixture, &principal));
    admin = dt_principal(LOCKER_ROLE_ADMIN, 0x80u);
    TEST_CHECK(dt_seed_principal(&fixture, &admin));

    /* 认证端口在 S1-04 只是占位：明确 NOT_READY，不伪造成功。 */
    slot = domain_completion_acquire(fixture.core, &token);
    TEST_CHECK(slot != UINT32_MAX);
    memset(&job, 0, sizeof(job));
    job.kind = (uint8_t)DOMAIN_JOB_AUTH;
    job.completion = slot;
    (void)snprintf(job.body.auth.token, sizeof(job.body.auth.token), "%s", "not-a-real-token");
    job.body.auth.token_length = strlen(job.body.auth.token);
    domain_handle_auth(fixture.core, &job);
    {
        locker_query_result_t result;
        memset(&result, 0, sizeof(result));
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, domain_completion_wait_final(fixture.core, slot, 0u, &result));
        TEST_CHECK(result.status == LOCKER_STATUS_NOT_READY);
    }
    domain_completion_release(fixture.core, slot);

    /* 维护核验：非 ADMIN 静默拒绝，不发 resume；ADMIN + 匹配的版本才放行一次。 */
    memset(&job, 0, sizeof(job));
    job.kind = (uint8_t)DOMAIN_JOB_MAINTENANCE;
    job.completion = UINT32_MAX;
    job.body.maintenance.peer.verified_admin_peer = true;
    job.body.maintenance.maintenance.principal = principal;
    job.body.maintenance.maintenance.physical_revision = 7u;
    job.body.maintenance.maintenance.snapshot_revision = fixture.core->revision;
    (void)snprintf(job.body.maintenance.maintenance.reason, sizeof(job.body.maintenance.maintenance.reason), "%s",
                   "reconcile after restart");
    domain_handle_maintenance(fixture.core, &job);
    TEST_CHECK_U64(0u, (uint64_t)fixture.device->resume_calls);

    job.body.maintenance.maintenance.principal = admin;
    domain_handle_maintenance(fixture.core, &job);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->resume_calls);
    TEST_CHECK(locker_uuid_equal(&fixture.device->last_guard.session_id, &fixture.core->session.session_id));
    TEST_CHECK_U64(fixture.core->session.boot_id, fixture.device->last_guard.expected_boot_id);
    TEST_CHECK_U64(7u, fixture.device->last_guard.expected_physical_revision);

    /* 版本不符不发。 */
    job.body.maintenance.maintenance.physical_revision = 9u;
    domain_handle_maintenance(fixture.core, &job);
    TEST_CHECK_U64(1u, (uint64_t)fixture.device->resume_calls);

    /* tick：推进时钟、跑超时、泵设备、发布进度，但不越权改状态。 */
    {
        uint64_t progress_before = fixture.core->progress_counter;
        memset(&job, 0, sizeof(job));
        job.kind = (uint8_t)DOMAIN_JOB_TICK;
        job.completion = UINT32_MAX;
        job.body.tick_ms = 1000u;
        domain_handle_tick(fixture.core, job.body.tick_ms);
        TEST_CHECK(fixture.core->progress_counter > progress_before);
        TEST_CHECK(fixture.core->progress_valid);
        TEST_CHECK(fixture.core->progress.target == LOCKER_PROGRESS_TARGET_SERVICE);
        TEST_CHECK(locker_uuid_equal(&fixture.core->progress.instance_id, &fixture.core->instance_id));
        TEST_CHECK_U64(fixture.core->progress_counter, fixture.core->progress.progress);

        /* 相同 counter 重复读取不续命：progress 只在 tick 里更新。 */
        {
            locker_progress_t snapshot;
            memset(&snapshot, 0, sizeof(snapshot));
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(fixture.core, &snapshot));
            TEST_CHECK_U64(fixture.core->progress.progress, snapshot.progress);
            memset(&snapshot, 0, sizeof(snapshot));
            TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_core_get_progress(fixture.core, &snapshot));
            TEST_CHECK_U64(fixture.core->progress.progress, snapshot.progress);
        }
    }

    /* 未知作业类型不动状态。 */
    {
        uint64_t revision_before = fixture.core->revision;
        memset(&job, 0, sizeof(job));
        job.kind = 200u;
        job.completion = UINT32_MAX;
        domain_handle_job(fixture.core, &job);
        TEST_CHECK_U64(revision_before, fixture.core->revision);
    }

    dt_fixture_free(&fixture);
}

int main(void)
{
    test_create_guards();
    test_create_success();
    test_create_idempotency();
    test_create_commit_failure();
    test_confirm();
    test_cancel();
    test_resolve();
    test_presence_and_arm();
    test_outbox_pump();
    test_timeouts();
    test_query_dispatch();
    test_job_dispatch();
    TEST_REPORT("domain.test_domain_commands");
}
